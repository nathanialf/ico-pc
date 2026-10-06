#include "typedef.h"
#include "enemy.h"
#include "act_a_p_1.h"
#include "sugiCommon.h"
#include "charFileManager.h"
#include "debug.h"
#include "debug_exception.h"
#include "memory.h"
#include "gobj.h"
#include "GifPacket.h"
#include "enemy_act.h"
#include "EnemyInit.h"
#include "Matrix.h"
#include "RegistPacket.h"
#include "enemyParts.h"
#include "geometryManager.h"
#include "lodManager.h"
#include "matrixDrive.h"
#include "motionManager2.h"
#include "motionOrientManager.h"
#include "particleEffect.h"
#include "quaternion.h"
#include <libvu0.h>
#include <stdlib.h>
#include "ios.h"
#include "main.h"
#include <assert.h>
#include "sceneManager.h"
#include "Primitive.h"

typedef struct { /* field names derived */
    float x;
    float y;
    float z;
    float w;
} EnemyPosEntry; /* derived name */

static inline void clearEnemyParticleFlags(int *p, int n) /* derived name */
{
    int i;
    for (i = 0; i < n; i++)
        p[i] = 0;
}

/* a unit matrix with the translation (10, 0, 0); nothing in the file reads
   it */
static float offsetMatrix[4][4] = {
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {10.0f, 0.0f, 0.0f, 1.0f},
}; /* derived name */

/* The 84-byte work record InitEnemyGeo allocates into the object's work
   word: the variation kind and the counter setEnemyObject randomises it with,
   the enemy-table row, the particle and broken-part tables, the two eyes, the
   footprints, the per-node particle flags, the object-loaded flag, the wing
   and stone parameters and the scale. */
typedef struct {              /* field names derived */
    int kind;                 /* 0x00 */
    int ctr;                  /* 0x04, setEnemyObject's randomiser state */
    int def;                  /* 0x08, the enemyKind row */
    int padC;                 /* 0x0C */
    PrimParticle **particle;  /* 0x10 */
    int *broken;              /* 0x14 */
    EnemyEye *eye0;           /* 0x18 */
    int word1C;               /* 0x1C */
    EnemyEye *eye1;           /* 0x20 */
    int word24;               /* 0x24 */
    EnemyFootPrintHead *foot; /* 0x28 */
    int footSwitch; /* 0x2C, nonzero while the enemy leaves footprints (SetEnemyFootPrintSwitch) */
    int *flag;      /* 0x30 */
    int pad34;      /* 0x34 */
    int loaded;     /* 0x38 */
    float float3C;  /* 0x3C */
    short short40;  /* 0x40 */
    float wing;     /* 0x44 */
    float scale;    /* 0x48 */
    int timer;      /* 0x4C */
    float flyXZAccel; /* 0x50, the fly XZ acceleration, from the enemyKind row */
} EnemyWork;          /* derived name */

static void setEnemyParticleObject(GObj *self, int pid)
{
    Sub15C *sub = GOBJ_SUB(self);
    EnemyWork *w = sub->work;
    int n = sub->skelNodeNum;
    SkelNode *tbl = sub->skel;
    struct DObjNode *p = sub->nodes;
    PrimParticle **parts;
    int *fl;
    float size;
    int i;
    int cnt;
    int num;
    int n4;
    int type;
    EnemyPosEntry *v;
    EnemyPosEntry *q;

    size = (p->scale[0] + p->scale[1] + p->scale[2]) * 32.0f * 0.33333f * 0.5f * 10.0f;
    parts = (PrimParticle **)iosMallocDebug(ios_partition_sugipon, n * sizeof(PrimParticle *),
                                            "src/enemy.c", 130);
    w->particle = parts;
    fl = (int *)iosMallocDebug(ios_partition_sugipon, n * 4, "src/enemy.c", 132);
    w->flag = fl;
    clearEnemyParticleFlags(fl, n);
    for (i = 0; i < n; i++) {
        q = (EnemyPosEntry *)enemy_GetPositionTable(pid, i);
        v = q;
        if (q == 0) {
            (w->flag)[i] = 0;
            continue;
        }
        (w->flag)[i] = 1;
        for (cnt = 0; q->w > -1.0f; cnt++)
            q++;
        q = v;
        parts[i] = 0;
        if (cnt <= 0)
            continue;
        if (cnt >= 80)
            num = 80;
        else
            num = cnt;
        parts[i] = prim_InitParticle(num, size, 0.5f, 0.5f, 1, "enemy_sprite", 0);
        if (parts[i] == 0) {
            /* EUC-JP: "cannot reserve the memory for the enemy soldier particles" */
            debug_StdPrintfDummy("敵兵のパーティクルのメモリを確保できません\n");
            debug_assertMessage("src/enemy.c", 177, "CAN'T ALLOCATE ENEMY'S PARTICLE MEMORY\n");
            __assert("src/enemy.c", 177, "e");
        }
        for (cnt = 0; q->w > -1.0f && cnt < 80; cnt++, q++) {
            n4 = rand() % 4;
            _CopyVector((char *)parts[i]->vtx + cnt * 32, q);
            _CopyVector((char *)parts[i]->vtxNext + cnt * 32, q);
            type = tbl[i].kind;
            if (type >= 38)
                goto spread;
            if (type < 36)
                goto spread;
            *(float *)((char *)parts[i]->vtx + cnt * 32 + 0x10) =
                *(float *)((char *)parts[i]->vtxNext + cnt * 32 + 0x10) = 0.5f;
            *(float *)((char *)parts[i]->vtx + cnt * 32 + 0x14) =
                *(float *)((char *)parts[i]->vtxNext + cnt * 32 + 0x14) = 0.0f;
            goto done;
        spread:
            *(float *)((char *)parts[i]->vtx + cnt * 32 + 0x10) =
                *(float *)((char *)parts[i]->vtxNext + cnt * 32 + 0x10) = (n4 / 2) * 0.5f;
            *(float *)((char *)parts[i]->vtx + cnt * 32 + 0x14) =
                *(float *)((char *)parts[i]->vtxNext + cnt * 32 + 0x14) = (n4 % 2) * 0.5f;
        done:
            *(float *)((char *)parts[i]->vtx + cnt * 32 + 0x18) =
                *(float *)((char *)parts[i]->vtxNext + cnt * 32 + 0x18) = 128.0f;
            *(float *)((char *)parts[i]->vtx + cnt * 32 + 0x1C) =
                *(float *)((char *)parts[i]->vtxNext + cnt * 32 + 0x1C) = 64.0f;
        }
    }
}

static inline int enemyRandomizeID(int kind, int *ctr) /* derived name */
{
    int lo = randomEnemyKind[kind - 0x10000].first;
    int n = randomEnemyKind[kind - 0x10000].last - lo;
    int id = lo + *ctr;

    debug_StdPrintfDummy("\x1b[36mRANDOMIZE COUNT: %d > RID: %d\x1b[m\n", *ctr, id);
    *ctr = *ctr + 1;
    if (*ctr >= n) {
        *ctr = 0;
    }
    return randomEnemyVariationKind[id];
}

static int setEnemyObject(GObj *self, int kind, int *ctr)
{
    struct DObjNode *p;
    Sub15C *sub;
    EnemyWork *w;
    float sc;
    int obj;
    int pid;

retry:
    sub = GOBJ_SUB(self);
    w = sub->work;
    if (kind > 0xFFFF) {
        kind = enemyRandomizeID(kind, ctr);
        goto retry;
    }
    p = sub->nodes;
    sc = enemyKind[kind].scale;
    p->scale[2] = sc;
    p->scale[1] = sc;
    p->scale[0] = sc;
    w->scale = sc;
    obj = enemyKind[kind].model;
    if (obj != 0x610) {
        GOBJ_SUB(self)->model = GetPObjAddress(obj);
        GOBJ_SUB(self)->modelId = obj;
        debug_StdPrintfDummy("%p\n", GOBJ_SUB(self)->model);
        w->loaded = 1;
    }
    pid = enemyKind[kind].particle;
    if (pid != -1) {
        setEnemyParticleObject(self, pid);
    }
    debug_StdPrintfDummy("\x1b[36mENEMY DESIGN ID: %d\x1b[m\n", kind);
    return kind;
}

/* One part's projected position: the sqc2 stores the integer x, y, z and w,
   and the part's index then takes the x word's place. */
typedef struct { /* field names derived */
    int idx;
    int y;
    int z;
    int w;
} EnemyDispEntry; /* derived name */

/* dispEnemyObject keeps the developer's own line layout: the lines without
 * code hold declarations, braces and comments.  The first loop's condition
 * reads the sub-object handle through a volatile int view; the body's own
 * read of the handle is GOBJ_SUB's int view. */
static void dispEnemyObject(void *self)
{
    float m[16];
    EnemyDispEntry *tmp;
    int i, j;
    int n = GOBJ_SUB(self)->skelNodeNum;
    SkelNode *tbl = GOBJ_SUB(self)->skel;
    EnemyWork *w = GOBJ_SUB(self)->work;
    PrimParticle **pl = w->particle;
    EnemyDispEntry buf[n];
    EnemyDispEntry *ptr[n];
    _SetCurrentMatrix((char *)GOBJ_SUB(self)->nodeMtx);
    _MulCurrentMatrixL(matrixptr + 0x100);

    _InitCurrentMatrix();

    /* clang-format off */
    for (i = 0; i < GOBJ_SUB(self)->skelNodeNum; i++) {
        /* project each part's origin: z goes into buf[i].z for the sort */
        ptr[i] = &buf[i];
        _SetCurrentMatrix((char *)GOBJ_SUB(self)->nodeMtx + i * 0x40);
        _MulCurrentMatrixL(matrixptr + 0x100);
        {
            /* the part origin through the current matrix, divided by w
               (x, y, z times 1/w), z to an integer; the PS2 also stored a
               stale vf13 into the entry's other words, which nothing reads */
            float v[4];
            float q;

            ico_apply_matrix(v, (const float (*)[4])ico_current_matrix, ZeroPoint);
            q = ps2_div(1.0f, v[3]);
            buf[i].z = ps2_ftoi(v[2] * q);
        }
        buf[i].idx = i;
    }

    /* sort far to near */
    for (i = 0; i < n; i++) {
        for (j = 0; j < n; j++) {
            if (i != j) {
                if (ptr[j]->z > ptr[i]->z) {
                    tmp = ptr[j];
                    ptr[j] = ptr[i];
                    ptr[i] = tmp;
                }
            }
        }
    }
    sceVu0UnitMatrix(m);

    for (j = 0; j < n; j++) {
        /* draw the parts still alive, far to near */
        i = ptr[j]->idx;
        if ((w->broken)[i] != 0) continue;
        if ((w->flag)[i] == 0) continue;

        switch (tbl[i].kind) {
        case 37:
            if (GOBJ_SUB(self)->nodes->fade == 0.0f)
                DispEnemyEye(w->eye0);
            break;
        case 36:
            if (GOBJ_SUB(self)->nodes->fade == 0.0f)
                DispEnemyEye(w->eye1);
            break;
        default:
            /* every other part is a particle */
            gif_StartPacketPri(6);
            gif_SetAlpha(1, 4, 128);
            gif_EndPacket();

            /* The particle is drawn in view space: m is the view matrix
               times the part's own matrix, the same product the first loop
               made on the current-matrix stack, rebuilt here because the
               stack no longer holds it. */

            sceVu0MulMatrix(m, matrixptr + 0x100, (char *)GOBJ_SUB(self)->nodeMtx + i * 0x40);
            prim_DispParticle(pl[i], m);
            break;
        }
    }
    /* clang-format on */
}

/* expanded into EnemyCheckHit, enemySetParticleDie, EnemySetfDisappear and
 * EnemyDeleteParticle; it returns SetParticleEffect's result */
static inline int enemySetParticle(int kind, void *obj, float *dir) /* derived name */
{
    float q[4];
    unsigned short ax, ay;
    MatrixDrive_GetTurnZAngleXY(&ax, &ay, dir[0], dir[1], -dir[2]);
    SetIdentityQuaternion(q);
    RotQuaternionX(q, (short)-ax);
    RotQuaternionY(q, (short)-ay);
    return SetParticleEffect(kind, obj, q);
}

int EnemyCheckHit(GObj *self, float *pos, float *dir)
{
    int eff = 0;
    Sub15C *sub;
    EnemyWork *w;
    int n;
    int i;
    int cnt;
    int flags;

    sub = self->dobj;
    w = sub->work;
    n = sub->skelNodeNum;
    cnt = 0;
    flags = 0;
    for (i = 0; i < n; i++) {
        if ((w->broken)[i] == 0) {
            if (distance_squared((char *)sub->nodeMtx + i * 64 + 0x30, pos) < 10000.0f) {
                enemySetParticle(8, (char *)sub->nodeMtx + i * 64 + 0x30, dir);
                /* The hand-written form the helper call replaced, the way
                   CheckEnemyHit still spells it, switched off by a local effect
                   switch, CheckEnemyHit's own `eff` with the other value. */
                if (eff) {
                    float q[4];
                    short rx;
                    short ry;

                    MatrixDrive_GetTurnZAngleXY(&rx, &ry, dir[0], dir[1], -dir[2]);
                    SetIdentityQuaternion(q);
                    RotQuaternionX(q, -rx);
                    RotQuaternionY(q, -ry);
                    SetParticleEffect(8, (char *)sub->nodeMtx + i * 64 + 0x30, q);
                }
                flags |= 1;
                (w->broken)[i] = 1;
                cnt++;
                GetSkeltonFocusNode(self, 34);
                if (i == GetSkeltonFocusNode(self, 35)) {
                    (w->broken)[GetSkeltonFocusNode(self, 36)] = 1;
                    (w->broken)[GetSkeltonFocusNode(self, 37)] = 1;
                }
                if (cnt >= 4) {
                    break;
                }
            }
        }
    }
    return flags;
}

/* CheckEnemyHit keeps the developer's own line layout: the lines without
 * code hold declarations, braces and comments.  The counter starts at 0 in a
 * statement of its own ahead of a test on n alone, and the for has no
 * initialiser; the particle calls sit under a test of a local constant. */
int CheckEnemyHit(GObj *self, float *pos, float *a, float *b)
{
    int eff = 1;
    Sub15C *sub = self->dobj;
    EnemyWork *w = sub->work;
    int i;
    int n = sub->skelNodeNum;

    if (2500.0f < distance_squared(pos, a)) {
        /* Walk the parts still alive and take the first one within reach
           of all three points: mark it hit, spawn the hit effect facing
           along a - pos, and report the hit.  The test on n shares its line
           with the for; the fence keeps that line, the braceless for and the
           condition below one statement to a line. */
        /* clang-format off */
        i = 0;
        if (n > 0) for (; i < n; i++)
            if ((w->broken)[i] == 0) {
                if (distance_squared((char *)sub->nodeMtx + i * 64 + 0x30, pos) < 10000.0f &&
                    distance_squared((char *)sub->nodeMtx + i * 64 + 0x30, a) < 10000.0f &&
                    distance_squared((char *)sub->nodeMtx + i * 64 + 0x30, b) < 10000.0f) {
                    float q[4];
                    unsigned short ax, ay;
                    if (eff) {
                        MatrixDrive_GetTurnZAngleXY(&ax, &ay,
                                                    -(pos[0] - a[0]),
                                                    -(pos[1] - a[1]),
                                                    -(pos[2] - a[2]));
                        SetIdentityQuaternion(q);
                        RotQuaternionX(q, (short)-ax);
                        RotQuaternionY(q, (short)-ay);
                        SetParticleEffect(8, (char *)sub->nodeMtx + i * 64 + 0x30, q);
                    }

                    (w->broken)[i] = 1;

                    return 1;
                }
            }
    }
    return 0;
    /* clang-format on */
}

/* The 0x15C slot is the engine's sub-object HANDLE: the code stores an int and
 * reads it back as a pointer, so every read of it is a union view, the same
 * spelling ico2/sugipon/src/geometryManager.c uses for the same slot. */

#define SUBOF(o) (((SubHandle *)&((GObj *)(o))->dobj)->sub) /* derived name */

/* the variation number the next enemy takes, stepped by two modulo ten */
static int enemyVariation = 0; /* derived name */

/* InitEnemyGeo's parts list; its body inlines clearEnemyParticleFlags */
static inline int enemyInitPartsList(GObj *self, SObjSimpleSetting *param) /* derived name */
{
    int kind = param->obj;
    EnemyWork *w;
    int n;
    int *parts;

    n = SUBOF(self)->skelNodeNum;
    /* the work-record entry is read as an int and cast */
    w = (EnemyWork *)SUBOF(self)->work;

    parts = (int *)iosMallocDebug(ios_partition_sugipon, n * 4, "src/enemy.c", 285);
    w->broken = parts;
    clearEnemyParticleFlags(parts, n);
    w->kind = kind;
    w->ctr = 0;
    return setEnemyObject(self, kind, &w->ctr);
}

void *InitEnemyGeo(GObj *self, SObjSimpleSetting *param)
{
    EnemyWork *w;
    int kind;
    int no;

    w = iosMallocDebug(ios_partition_sugipon, ICO_MAX_SIZE(EnemyWork, 0x54), "src/enemy.c", 641);
    SUBOF(self)->work = w;
    w->word1C = 0;
    w->eye0 = InitEnemyEye(10, 0, 10);
    w->word24 = 0;
    w->eye1 = InitEnemyEye(10, 0, 10);
    w->foot = InitEnemyFootPrint(6);
    w->footSwitch = 1;
    w->particle = 0;
    w->loaded = 0;
    w->float3C = 0.0f;
    w->short40 = 0;
    w->wing = 0.0f;
    w->timer = 0;
    w->flyXZAccel = 1.0f;
    kind = enemyInitPartsList(self, param);
    w->def = kind;
    w->flyXZAccel = enemyKind[kind].float0C;
    InitMotionOrient(self, 0x84A, 0x967, 0x18, 0x24, 0x342);
    no = enemyVariation;
    SUBOF(self)->ctrl.variation = no;
    enemyVariation = (no + 2) % 10;
    SUBOF(self)->ctrl.catchBoy = 0;
    SetLodLevel(self, 2);
    return w;
}

void EnemyGeo(GObj *self)
{
    Sub15C *sub = GOBJ_SUB(self);
    Act *node = GOBJ_ACT(self);
    unsigned long long flag = node->flags18.ll;
    EnemyWork *w = sub->work;
    float ratio;
    float buf[4];

    if ((int)(flag >> 33) & 1) {
        w->timer = 0;
    } else {
        if (w->timer >= 0xB)
            return;
        w->timer = w->timer + 1;
    }

    GOBJ_SUB(self)->ctrl.catchBoy = 0;
    GOBJ_SUB(self)->ctrl.cliffWallCheck = 2;
    GOBJ_SUB(self)->ctrl.wallReact = 0;
    if (GetEnemyTypeFromGObj(self) == 3)
        GOBJ_SUB(self)->ctrl.catchBoy = 1;

    ExecMotionOrient(self);

    CylinderCollisionWithControlDynamics(self, 4, 0, w->scale * 70.0f, w->scale * 50.0f, 0.5f);

    if (isEnemyActive(self) != 0) {
        Sub15C *s = GOBJ_SUB(self);
        if (s->ctrl.landed != 0) {
            if (w->footSwitch != 0) {
                if (!(s->ctrl.motion == 0x3A1 || s->ctrl.motion == 0x3A2)) {
                    GetProjectionOfPlane(
                        buf, s->root.plane.f,
                        (float *)((char *)s->nodeMtx + s->root.standNode * 0x40 + 0x30));
                    EntryEnemyFootPrint(w->foot, buf);
                }
            }
        }
    }
    ExecEnemyFootPrints(w->foot);

    GOBJ_SUB(self)->ctrl.variation = (GOBJ_SUB(self)->ctrl.variation + 1) % 10;

    ratio = (GOBJ_SUB(self)->nodes->scale[0] + GOBJ_SUB(self)->nodes->scale[1] +
             GOBJ_SUB(self)->nodes->scale[2]) /
            3.0f;

    _MulMatrix(MatrixDrive_GetMatrix(),
               (char *)GOBJ_SUB(self)->nodeMtx + GetSkeltonFocusNode(self, 0x24) * 0x40,
               (char *)offsetMatrix);
    UpdateEnemyEye(w->eye0, MatrixDrive_GetMatrix(), ratio);
    _MulMatrix(MatrixDrive_GetMatrix(),
               (char *)GOBJ_SUB(self)->nodeMtx + GetSkeltonFocusNode(self, 0x25) * 0x40,
               (char *)offsetMatrix);
    UpdateEnemyEye(w->eye1, MatrixDrive_GetMatrix(), ratio);
}

void DisplayEnemy(GObj *self)
{
    EnemyWork *w = GOBJ_SUB(self)->work;

    if (w->loaded != 0) {
        reg_DispEnemy((char *)GOBJ_SUB(self));
        if (GOBJ_SUB(self)->nodes->fade == 0.0f) {
            DispEnemyEye(w->eye0);
            DispEnemyEye(w->eye1);
        }
    }
    DispEnemyFootPrints(w->foot);
    if (w->particle != 0) {
        dispEnemyObject(self);
    }
}

void EnemyDL(GObj *self)
{
    Act *sub = GOBJ_ACT(self);
    unsigned long long flag = sub->flags18.ll;
    if (((flag >> 33) & 1) == 0)
        return;
    IsActCharDead(self);
    if (isEnemyHyde(self) != 0)
        return;
    DisplayEnemy(self);
}

void DemoMotionGeo(GObj *self)
{
    GOBJ_SUB(self)->root.hand1.mode = 0;
    GOBJ_SUB(self)->root.hand0.mode = 0;
    GOBJ_SUB(self)->root.lookIK = 0;
    GOBJ_SUB(self)->root.handTurnIK = 0;
    ExecMotionOrient(self);
}

void SetEnemyDissolve(GObj *self, float ratio)
{
    Sub15C *sub = GOBJ_SUB(self);

    sub->nodes->fade = ratio;
    if (sub->nodes->fade < 0.0f)
        sub->nodes->fade = 0.0f;
    if (sub->nodes->fade > 1.0f)
        sub->nodes->fade = 1.0f;
}

static void SetEnemyFlyXZAccel(GObj *self, float accel)
{
    EnemyWork *w = GOBJ_SUB(self)->work;

    w->flyXZAccel = accel;
}

static void SetEnemyFlyXZAccelAll(float accel)
{
    GObj *g = isysGObjSearchFromObjKindID_begin(4);
    while (g != 0) {
        EnemyWork *w = GOBJ_SUB(g)->work;

        w->flyXZAccel = accel;
        g = isysGObjSearchFromObjKindID_next(g);
    }
}

float GetEnemyFlyXZAccel(GObj *self)
{
    EnemyWork *w = GOBJ_SUB(self)->work;

    return w->flyXZAccel;
}

void EnemyAI(void) {}

void SetEnemyFootPrintSwitch(GObj *self, int on)
{
    EnemyWork *w = GOBJ_SUB(self)->work;

    w->footSwitch = on;
}

void EnemySetfAppearAll(GObj *self)
{
    int n = GOBJ_SUB(self)->skelNodeNum;
    int i;

    for (i = 0; i < n; i++)
        ((EnemyWork *)GOBJ_SUB(self)->work)->broken[i] = 0;
}

void EnemySetfDisappearAll(GObj *self)
{
    int n = GOBJ_SUB(self)->skelNodeNum;
    int i;

    for (i = 0; i < n; i++)
        ((EnemyWork *)GOBJ_SUB(self)->work)->broken[i] = 1;
}

void EnemySetfDisappear(GObj *self, float *dir)
{
    Sub15C *sub = GOBJ_SUB(self);
    int n = sub->skelNodeNum;
    EnemyWork *w = sub->work;
    int i;

    for (i = 0; i < n; i++) {
        if ((w->broken)[i] == 0) {
            (w->broken)[i] = 1;
            enemySetParticle(8, ICO_RAW(char *, sub, 12, (char *)sub->nodeMtx) + i * 64 + 48, dir);
            return;
        }
    }
}

void enemySetParticleDie(void *root, float *dir)
{
    float q[4];
    unsigned short ax, ay;
    MatrixDrive_GetTurnZAngleXY(&ax, &ay, dir[0], dir[1], -dir[2]);
    SetIdentityQuaternion(q);
    RotQuaternionX(q, (short)-ax);
    RotQuaternionY(q, (short)-ay);
    SetParticleEffect(12, root, q);
}

void ReviveEnemyParticle(GObj *self, int node)
{
    EnemyWork *w = GOBJ_SUB(self)->work;

    w->broken[node] = 0;
}

int isExistEnemyParticle(GObj *self, int node)
{
    EnemyWork *w = GOBJ_SUB(self)->work;

    return w->broken[node] == 0;
}

int EnemyGetNSafeParts(GObj *self)
{
    int n = GOBJ_SUB(self)->skelNodeNum;
    int *p;
    int i;
    int cnt = 0;

    for (i = 0; i < n; i++) {
        p = ((EnemyWork *)GOBJ_SUB(self)->work)->broken;
        if (p[i] == 0)
            cnt++;
    }
    return cnt;
}

void EnemyDeleteParticle(GObj *self, float *dir, short *list)
{
    Sub15C *sub = self->dobj;
    int i;
    int n;

    n = 2;
    for (i = 0; list[i] >= 0 && n > 0; i++, n--) {
        enemySetParticle(8, (char *)sub->nodeMtx + list[i] * 64 + 48, dir);
    }
}

void SetEnemyHitGeometryAction(GObj *self, int on)
{
    EnemyWork *w = GOBJ_SUB(self)->work;

    w->loaded = on;
}

int InitDemoMotionGeo(GObj *self)
{
    InitMotionOrient(self, 2122, 2407, -1, -1, 983);
    SetLodLevel(self, 0);
    self->active = 0;
    return 0;
}

void HotInitDemoMotionGeo(GObj *self)
{
    InitMotionOrient(self, 2122, 2407, -1, -1, 983);
    SetLodLevel(self, 0);
    self->active = 0;
}

int *GetEnemyHitNodeFlag(GObj *self)
{
    EnemyWork *w = GOBJ_SUB(self)->work;

    return w->broken;
}

int RandomizeEnemy(GObj *self)
{
    Sub15C *sub = GOBJ_SUB(self);
    EnemyWork *w = sub->work;
    int kind = w->kind;

    sub->nodes->fade = 0.0f;
    return setEnemyObject(self, kind, &w->ctr);
}

void SetEnemyWingRatio(GObj *self, float ratio)
{
    ((EnemyWork *)GOBJ_SUB(self)->work)->wing = ratio;
}

int CanThisEnemyFly(GObj *self)
{
    return enemyKind[((EnemyWork *)GOBJ_SUB(self)->work)->def].flyType;
}

int GetEnemyBattleType(GObj *self)
{
    return enemyKind[((EnemyWork *)GOBJ_SUB(self)->work)->def].battleType;
}

float GetEnemyDefLife(GObj *self)
{
    return enemyKind[((EnemyWork *)GOBJ_SUB(self)->work)->def].life;
}

float GetEnemyDefDodgeRange(GObj *self)
{
    return enemyKind[((EnemyWork *)GOBJ_SUB(self)->work)->def].dodge;
}

float GetEnemyDefParaIndex(GObj *self)
{
    return enemyKind[((EnemyWork *)GOBJ_SUB(self)->work)->def].paraIndex;
}

void ResetEnemyPositionInfo(GObj *self)
{
    EnemyWork *w = GOBJ_SUB(self)->work;

    ResetEnemyEye(w->eye0);
    ResetEnemyEye(w->eye1);
    GOBJ_SUB(self)->ctrl.blendFrames =
        (int)((float)((0x3C - systemStatus[0] * 0xA) / systemStatus[1]) / 60.0f * 0.0f);
}

void SetEnemyStonizedVisual(GObj *self)
{
    int local[8];
    GetRootPosition(local, self);
    GetRootQuaternion(&local[4], self);
    SetParticleEffect(0x31, local, &local[4]);
    self->active = 0;
}
