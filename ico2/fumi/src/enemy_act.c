#include "enemy_act.h"
#include "debug.h"
#include "gamesys.h"
#include "gobj.h"
#include "obj_manager.h"
#include "gobj_process.h"
#include "act-game.h"
#include "act.h"
#include "boyact.h"
#include "gather_effect.h"
#include "camera-editor.h"
#include "ebrain.h"
#include "generator.h"
#include "enemy.h"
#include "geometryManager.h"
#include "motionOrientManager.h"
#include "quaternion.h"
#include <string.h>
#include "commonact.h"
#include "gflag.h"
#include "StageManager.h"
#include "backStage.h"
#include "matrixDrive.h"
#include "StageAnimation.h"
#include "darkVolume.h"
#include "sugiCommon.h"
#include <libvu0.h>
#include "act-way.h"
#include "isys.h"
#include "Matrix.h"
#include "GifPacket.h"
#include "debug_exception.h"
#include "enemy-control.h"
#include "Primitive.h"
#include "multiBgaManager.h"
#include "gv.h"
#include "main.h"
#include <assert.h>
#include "motionManager2.h"
#include "attackhit.h"
#include "pad.h"
#include "flyManager.h"

int entesty;

/* The brain-mode table, one 28-byte record per mode: its name, its priority
   against the running mode, the brain function and four parameters
   (subEnemyBrainMain and BrainMode_Requset read them). */
typedef struct { /* field names derived */
    char *name;
    int pri;
    void (*brain)(GObj *);
    int aim;       /* 0x0C, copied to the actor's brainAim: 1 the girl, 2 the boy */
    int word10;    /* 0x10, read by nothing in the retail build */
    int stoppable; /* 0x14, nonzero: mail 0x20 stops the brain until mail 0x1F */
    int infoPos;   /* 0x18, copied to the actor's infoPos */
} EnemyBrainMode;  /* derived name */

void subEnemyBrain_ToBoy(GObj *volatile self);
void subEnemyBrain_ToGirl(GObj *volatile self);
void subEnemyBrain_ToGenerator(GObj *self);
void subEnemyBrain_Cling(GObj *volatile self);
void subEnemyBrain_Attack(GObj *volatile self);

EnemyBrainMode brainModeTable[] = {
    {"START", 0, subEnemyBrain_Idle, 0, 0, 1, 0},
    {"IDLE", 1, subEnemyBrain_Idle, 0, 0, 1, 0},
    {"AWAIT", 2, subEnemyBrain_Await, 2, 2, 1, 1},
    {"TO_BOY", 2, subEnemyBrain_ToBoy, 2, 2, 1, 1},
    {"TO_GIRL", 2, subEnemyBrain_ToGirl, 1, 3, 1, 2},
    {"TO_GENE", 2, subEnemyBrain_ToGenerator, 0, 1, 1, 4},
    {"FIND_GIRL", 2, subEnemyBrain_FindGirl, 0, 0, 1, 0},
    {"BODYGUARD", 2, subEnemyBrain_BodyGuard, 0, 4, 1, 5},
    {"CLING", 3, subEnemyBrain_Cling, 0, 0, 1, 0},
    {"ATTACK", 3, subEnemyBrain_Attack, 0, 0, 1, 0},
    {"SHOULDER", 3, subEnemyBrain_Shoulder, 0, 3, 1, 2},
    {"PICKUP", 3, subEnemyBrain_Pickup, 0, 1, 1, 4},
    {"BODYSLAM", 3, subEnemyBrain_Bodyslam, 0, 0, 1, 0},
    {"IRREGULAR", 4, subEnemyBrain_Irregular, 0, 0, 1, 0},
}; /* derived name */

/* The default target, read when a mode is set with no target:
   _BrainMode_SetDirect's else arm and the two nested brain-change children
   start from it. */
static const BrainModeTarget brainTargetNone = {0}; /* derived name */

#define BOSS_START_WORK(self) ((int)GOBJ_ACT(self)->enemy) /* derived name */

typedef struct { /* field names derived */
    char pad00[20];
    int id;
    int timer;
    char busy;
    char alive;
    char pad1E[2];
} BossPart; /* derived name */

#define BOSS_EFFECT_WORK(self) ((char *)GOBJ_ACT(self)->enemy) /* derived name */
#define BOSS_EFFECT_PARTS(self, i)                                                                 \
    ((BossPart *)((i) * 0x20 + BOSS_EFFECT_WORK(self) + 0x360)) /* derived name */

/* motionOrientManager.h declares none of the motion tables */
extern MotionDef motionKind[];

/* this TU's uses of _GetMotionDirection do not fit the prototype in
   motionManager2.h */

/* The DEBUG build holds the enemy's stick poll while the debug flag word's
   hold bit is set, a frame at a time, the way boyact.c's subBoyControl repeats
   its stick loop with _ACTWait; retail builds it as 0. */
#ifdef DEBUG
#define ENEMY_DEBUG_HOLD (debug_font_flag & 0x200) /* derived name */
#else
#define ENEMY_DEBUG_HOLD 0 /* derived name */
#endif

/* enemy.c's; no header declares it */
extern float GetEnemyDefParaIndex(void *self);

/* The point the lifting enemy turns to: three vectors (the first two equal),
   of which only the first is addressed. */
static sceVu0FVECTOR bodyliftTarget[3] = {
    {-311.0f, -89.0f, -147.0f, 0.0f},
    {-311.0f, -89.0f, -147.0f, 0.0f},
    {-770.0f, -1445.0f, -749.0f, 0.0f},
}; /* derived name */

/* FLT_MAX word in .sdata, declared as an incomplete array */
extern void SetEnemyStonizedVisual(void *self);
extern void BossEnemyFunc(void *self);

/* the TU's flag-test shape, a 64-bit shift-and-mask of a 32-bit word, as a
   function-like macro */
#define EA_CHKBIT(f, n) (((int)((long long)(f) >> (n))) & 1) /* derived name */

/* GetFlyPosition's points: the four the enemy measures against, the four it
   flies to (paired by index, 200 below), and the one it escapes to. */
static sceVu0FVECTOR flyCheckPos[4] = {
    {760.0f, 0.0f, 766.0f, 1.0f},
    {708.0f, 0.0f, -806.0f, 1.0f},
    {-1394.0f, 0.0f, -858.0f, 1.0f},
    {-1383.0f, 0.0f, 645.0f, 1.0f},
}; /* derived name */

static sceVu0FVECTOR flyDestPos[4] = {
    {842.0f, -200.0f, 1278.0f, 1.0f},
    {734.0f, -200.0f, -1273.0f, 1.0f},
    {-1394.0f, -200.0f, -1291.0f, 1.0f},
    {-1383.0f, -200.0f, 1291.0f, 1.0f},
}; /* derived name */

static sceVu0FVECTOR flyEscapePos = {1712.0f, -600.0f, 0.0f, 1.0f}; /* derived name */

/* The brain-mode target the ChangeBrain_ToAttack and ChangeBrain_ToKidnap
   children fill in from the default and hand to _BrainMode_SetDirect: one
   record shared by the nested functions of two parents, so file scope (the
   TU's whole .sbss). */
static BrainModeTarget brainTarget; /* derived name */

/* One start record per motion phase; the four of them are the actor's whole
   start parameter block. */
typedef struct { /* field names derived */
    int sizeClass;
    int paraStatus;
    int clingNode;
    int attackChance;
    int attackChance2;
    float maxLife;
    float bodyslamMail;
    unsigned int noGuard; /* the status word's bit 51, which turns away the guard mail */
} EnemyStartRec;          /* derived name */

/* The gobj's sub-object slot at +0x15C, an int handle the engine also reads
   as the sub record's address (see GOBJ_SUB in typedef.h), as a union of the
   two views. */
typedef union { /* field names derived */
    int handle;
    char *p;
} EnemySubSlot; /* derived name */

/* The actor's character kind at act+0x48, the index act.c's after_func_exec
   and BeforeFunc read into the status table's six-entry rows; actInitialize
   sets it to -1, actGirlStart to 1 and actEnemyStart to 2. */
typedef enum { ACT_KIND_NONE = -1, ACT_KIND_GIRL = 1, ACT_KIND_ENEMY = 2 } ActKind;

#define ENEMY_START_WORK(self) ((int)GOBJ_ACT(self)->enemy) /* derived name */

typedef struct { /* field names derived */
    char pad00[32];
    long long flags;
} EnemyBrainWork; /* derived name */

inline int IsEnemyBrainToGenerator(GObj *self, GObj **out)
{
    Act *b = GOBJ_ACT(self);
    if (b->enemy->mode != 5)
        return 0;
    *out = ((ActWork *)b->work)->genTarget;
    if (*out == 0) {
        debug_assert("src/enemy_act.c", 0x341);
        __assert("src/enemy_act.c", 0x341, "*generator_gop!=NULL");
    }
    return 1;
}

inline int IsEnemyBrainToBoy(GObj *self)
{
    Act *sub;
    EnemyBattleWork *sub2;
    if ((char *)girlGObj != 0) {
        Act *sub_d = GOBJ_ACT(girlGObj);
        if (sub_d->actMode != 0x6F)
            return 0;
    }
    sub = GOBJ_ACT(self);
    sub2 = sub->enemy;
    return sub2->mode == 3;
}

static void setBattleStatus(GObj *self)
{
    switch (GOBJ_ACT(self)->enemy->battleType) {
    case 0:
        GOBJ_ACT(self)->enemy->flags.ll &= ~1LL;
        GOBJ_ACT(self)->enemy->flags.ll &= ~2LL;
        break;
    case 1:
        GOBJ_ACT(self)->enemy->flags.ll &= ~1LL;
        GOBJ_ACT(self)->enemy->flags.ll |= 2LL;
        break;
    case 2:
        GOBJ_ACT(self)->enemy->flags.ll |= 1LL;
        GOBJ_ACT(self)->enemy->flags.ll &= ~2LL;
        break;
    case 3:
        GOBJ_ACT(self)->enemy->flags.ll |= 1LL;
        GOBJ_ACT(self)->enemy->flags.ll |= 2LL;
        break;
    default:
        debug_assert("src/enemy_act.c", 0x36B);
        __assert("src/enemy_act.c", 0x36B, "0");
    }
}

inline void boss_effect_callback(int id)
{
    GObj *g;
    int i;
    char *p;
    for (g = isysGObjSearchFromObjKindID_begin(4); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        if (GOBJ_ACT(g)->enemy->liftKind == 3) {
            for (i = 0; i < 5; i++) {
                p = (char *)(i * 0x20 + (int)GOBJ_ACT(g)->enemy + 0x360);
                if (p[0x1D] != 0 && *(int *)(p + 0x10) == id) {
                    p[0x1C] = 0;
                    return;
                }
            }
        }
    }
}

/* a static inline, inlined by both boss_effect_start and
   boss_effect_process */
static inline void bossEffectSetNodePos(GObj *self, float *dst, int idx) /* derived name */
{
    Sub15C *g = GOBJ_SUB(self);

    sceVu0CopyVector(dst, (float *)((char *)g->nodeMtx + idx * 0x40 + 0x30));
    dst[3] = 1.0f;
}

static void boss_effect_start(GObj *self, int id)
{
    int i;

    for (i = 0; i < 5; i++) {
        if (*(char *)(i * 0x20 + BOSS_START_WORK(self) + 0x37D) == 0) {
            float buf[4] = {0.0f, 0.0f, 0.0f, 1.0f};

            bossEffectSetNodePos(self, (float *)(i * 0x20 + BOSS_START_WORK(self) + 0x360), id);
            *(int *)(i * 0x20 + BOSS_START_WORK(self) + 0x370) =
                GatherEffect_Set(12, (char *)BOSS_START_WORK(self) + (i * 0x20 + 0x360), buf,
                                 (char *)BOSS_START_WORK(self) + (i * 0x20 + 0x360), 1.0f,
                                 (void *)boss_effect_callback);
            *(int *)(i * 0x20 + BOSS_START_WORK(self) + 0x374) = id;
            *(int *)(i * 0x20 + BOSS_START_WORK(self) + 0x378) =
                (60 - systemStatus[0] * 10) / systemStatus[1];
            *(char *)(i * 0x20 + BOSS_START_WORK(self) + 0x37C) = 1;
            *(char *)(i * 0x20 + BOSS_START_WORK(self) + 0x37D) = 1;
            return;
        }
    }
    ReviveEnemyParticle(self, id);
}

static void boss_effect_check_parts(GObj *self, int id)
{
    char *p = (char *)GOBJ_ACT(self)->enemy + 0x360;
    int i;
    for (i = 0; i < 5; i++, p += 0x20) {
        if (p[0x1D] != 0 && *(int *)(p + 0x14) == id) {
            return;
        }
    }
    boss_effect_start(self, id);
}

static void boss_effect_process(GObj *self)
{
    float tmp[4];
    int n;
    int i;

    n = GOBJ_SUB(self)->skelNodeNum;
    for (i = 0; i < n; i++) {
        if (isExistEnemyParticle(self, i) == 0) {
            boss_effect_check_parts(self, i);
        }
    }
    for (i = 0; i < 5; i++) {
        if (BOSS_EFFECT_PARTS(self, i)->alive == 0) {
            continue;
        }
        if (BOSS_EFFECT_PARTS(self, i)->busy != 0) {
            bossEffectSetNodePos(self, tmp, BOSS_EFFECT_PARTS(self, i)->id);
            GatherEffect_SetGoal(*(int *)((char *)(i * 0x20 + BOSS_EFFECT_WORK(self)) + 0x370),
                                 tmp);
        }
        if (BOSS_EFFECT_PARTS(self, i)->timer == 0) {
            ReviveEnemyParticle(self, BOSS_EFFECT_PARTS(self, i)->id);
        }
        if (BOSS_EFFECT_PARTS(self, i)->busy == 0 && BOSS_EFFECT_PARTS(self, i)->timer < 0) {
            BOSS_EFFECT_PARTS(self, i)->alive = 0;
        }
        BOSS_EFFECT_PARTS(self, i)->timer -= 1;
    }
}

static void _DoAwait(GObj *self)
{
    MotionDef *row;
    if (boyGObj != 0) {
        _ACTParaStatus_Set(self, 0x1C);
        row = &motionKind[GOBJ_SUB(self)->ctrl.motion];
        if ((row->flags.word >> 3) & 1) {
            EnemyUtil_TurnToBoy(self, boyGObj, 5);
        }
    }
}

static void _DoAwaitGirl(GObj *self)
{
    MotionDef *row;
    if ((char *)girlGObj != 0) {
        _ACTParaStatus_Set(self, 0x1C);
        row = &motionKind[GOBJ_SUB(self)->ctrl.motion];
        if ((row->flags.word >> 3) & 1) {
            EnemyUtil_TurnToBoy(self, girlGObj, 5);
        }
    }
}

static int _MustChase(GObj *self)
{
    float v1[4];
    float v2[4];
    float angle;
    float diff;
    int rv;
    if ((void *)boyGObj == 0) {
        goto zero;
    }
    v1[0] = test_CURRENTROOT(((void *)boyGObj))[0];
    v1[1] = test_CURRENTROOT(((void *)boyGObj))[1];
    v1[2] = test_CURRENTROOT(((void *)boyGObj))[2];
    v2[0] = test_CURRENTROOT(self)[0];
    v2[1] = test_CURRENTROOT(self)[1];
    v2[2] = test_CURRENTROOT(self)[2];
    angle = _DistxzSqGV(v1, v2);
    if (angle < 90000.0f) {
        diff = v1[1] - v2[1];
        if (diff < 0.0f) {
            if (200.0f < -diff) {
                return 1;
            }
            return 0;
        }
        rv = 0;
        if (!(200.0f < diff)) {
            return rv;
        }
    }
    rv = 1;
    goto end;
zero:
    rv = 0;
end:
    return rv;
}

/* A static inline between setBattleStatus and boss_effect_callback, expanded
   only in subEnemyControl. */
static inline void enemyPollHitNodes(GObj *self) /* derived name */
{
    int n = GOBJ_SUB(self)->skelNodeNum;
    int i;

    for (i = 0; i < n; i++) {
        GetEnemyHitNodeFlag(self);
    }
}

void subEnemyControl(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    float pos[4];
    float dir[4];
    int runCnt = 0;
    int walkCnt = 0;
    int stopCnt = 0;

    iosPadConnect(&sub->pad, 0, 1, &sub->padConf);
    while (1) {
        enemyPollHitNodes(self);
        /* the stick poll loop, subBoyControl's shape, repeating only under the
           DEBUG hold (retail breaks after one pass) */
        for (;;) {
            if (((int)(sub->flags18.ll >> 48)) & 1) {
                if (self == CurrentTargetGObj) {
                    iosPadConnect(&sub->pad, 0, 0, &sub->padConf);
                    iosPadRead(&sub->pad);
                    iosPadGetStick(&sub->pad, &sub->stick, 0, 2, 2, 0);
                    _GetMotionDirection(dir, self);
                    sub->stick.angle = CorrectStickInfo(dir, &sub->stick);
                    if (0.001f < sub->stick.mag) {
                        ConvertStickToAbsCoord(pos, &sub->stick);
                        sub->dir[0] = pos[0];
                        sub->dir[1] = pos[1];
                        sub->dir[2] = pos[2];
                    }
                } else if (self == CurrentTargetGObjSub) {
                    iosPadConnect(&sub->pad, 0, 1, &sub->padConf);
                } else {
                    iosPadConnect(&sub->pad, 0, 1, &sub->padConf);
                }
            }
            if (!ENEMY_DEBUG_HOLD) {
                break;
            }
            _ACTWait(1);
        }
        /* the whole counter update */
        stopCnt++;
        if (0.1f < sub->stick.mag) {
            stopCnt = 0;
        }
        if (0.1f < sub->stick.mag && (sub->stick.mag < 0.99f || (sub->pad.now & 0x20))) {
            walkCnt++;
        } else {
            walkCnt = 0;
        }
        /* moving and not walking, with the walking predicate repeated whole
           inside the negation, as commonact.c's _ACTCommonMailTest writes
           it */
        if (0.1f < sub->stick.mag &&
            !(0.1f < sub->stick.mag && (sub->stick.mag < 0.99f || (sub->pad.now & 0x20)))) {
            runCnt++;
        } else {
            runCnt = 0;
        }
        pos[0] = sub->dir[0];
        pos[1] = sub->dir[1];
        pos[2] = sub->dir[2];
        _ACTCommonMailTest(self, stopCnt, walkCnt, runCnt);
        switch (sub->actMode) {
        case 1:
            ACTSendMailCorrect((void *)self, 0xC7);
            break;
        case 2:
            if (0.1f < sub->stick.mag && (sub->stick.mag < 0.99f || (sub->pad.now & 0x20)) &&
                !(walkCnt < 4)) {
                if (CheckFloorAttribute(self, 0x200)) {
                    ACTSendMailCorrect((void *)self, 0xB6);
                } else {
                    ACTSendMailCorrect((void *)self, 0xB5);
                }
            }
            break;
        case 3:
            ACTSendMailCorrect((void *)self, 0xBA);
            break;
        /* a fifth case label, its value unknown: every index from 4 to 37 of
           the jump table goes to the break */
        case 4:
            break;
        case 38:
            if (100 < sub->stick.y - 128) {
                ACTSendMailCorrect((void *)self, 0x14B);
            } else if (sub->stick.y - 128 < -100) {
                ACTSendMailCorrect((void *)self, 0x14A);
            } else {
                ACTSendMailCorrect((void *)self, 0x150);
            }
            break;
        }
        _ACTWait(1);
    }
}

/* A static inline, a helper defined above EnemyUtil_TurnToBoy,
   _ApproachTarget_Boss and subEnemyCollision and inlined at each call. */
static inline unsigned char enemyCheckTurnAngle(GObj *self) /* derived name */
{
    float mot[4];
    float cur[4];
    Act *s = GOBJ_ACT(self);
    int limit = (s->actMode == 3) ? 0x5A : 0x69;
    int ang;
    int aang;

    cur[0] = s->dir[0];
    cur[1] = s->dir[1];
    cur[2] = s->dir[2];
    GetRootMotionOrient(mot, self);
    ang = _RotyGV(mot, cur);
    aang = ang < 0 ? -ang : ang;
    if (limit < aang) {
        s->env.turnDir.f[0] = cur[0];
        s->env.turnDir.f[1] = cur[1];
        s->env.turnDir.f[2] = cur[2];
        if (ang > 0) {
            ACTSendMailCorrect(self, 0xE8);
        } else {
            ACTSendMailCorrect(self, 0xE7);
        }
        return 1;
    } else if (aang < 0xF) {
        ACTSendMailCorrect(self, 0xF1);
    }
    return 0;
}

/* a static inline helper, inlined here and in subEnemyBrain_Irregular */
static inline unsigned char isEnemyCarriedByGirl(GObj *self) /* derived name */
{
    Act *gsub;
    if (GOBJ_ACT(self)->carried == 0 || (char *)girlGObj == 0) {
        return 0;
    }
    gsub = GOBJ_ACT(girlGObj);
    if ((char *)gsub == 0 || gsub->actMode != 0x6F) {
        return 0;
    }
    if (gsub->carrier == self) {
        return 1;
    }
    return 0;
}

void subEnemyCollision(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    int idx;

    while ((int)sub->motReq == 0) {
        _ACTWait(1);
    }
    while (1) {
        float *dir = (float *)((char *)sub + 0x120);
        if (actEnemyFlagCheckActive(self) != 0) {
            *(long long *)((char *)sub + 0x18) = (long long)sub->flags18.ll | (1LL << 32);
        } else {
            *(long long *)((char *)sub + 0x18) = (long long)sub->flags18.ll & ~(1LL << 32);
        }
        if ((((int)((long long)sub->flags18.ll >> 32)) & 1) == 0 && sub->actMode != 0x16) {
            *(long long *)((char *)sub + 0x18) = (long long)sub->flags18.ll & ~(1LL << 33);
        } else {
            *(long long *)((char *)sub + 0x18) = (long long)sub->flags18.ll | (1LL << 33);
        }
        if (GOBJ_ACT(self)->enemy->paraStatus != 0) {
            _ACTParaStatus_Set(self, GOBJ_ACT(self)->enemy->paraStatus);
        }
        if (GOBJ_ACT(self)->enemy->liftKind == 3) {
            GOBJ_ACT(self)->enemy->slowTimer -= 1;
            if (0 < GOBJ_ACT(self)->enemy->slowTimer) {
                float rate = (60 - GOBJ_ACT(self)->enemy->slowTimer) / 60.0f;
                float speed = (rate < 0.1f) ? 0.1f : ((1.0f < rate) ? 1.0f : rate);
                ACTGame_SetMotionPlaySpeedRatio_Reserve(self, speed, 8);
            }
        }
        ACTGame_CommonLoop((void *)self);
        CommonAttackCenter(self);
        if (GOBJ_ACT(self)->enemy->liftKind == 3) {
            boss_effect_process(self);
        }
        if (sub->actMode == 5 && 400.0f < GOBJ_SUB(self)->ctrl.groundHeight) {
            FlyMail((void *)self);
        }
        if (sub->stick.mag != 0.0f) {
            enemyCheckTurnAngle(self);
        }
        if ((stage_no == 19 || stage_no == 28) && sub->actMode == 6) {
        } else if (0.1f < sub->stick.mag && sub->actMode != 0x73) {
            SetMotionDirectionSmooze(
                self, dir,
                (float)((self == girlGObj && girlControlMode != 0)
                            ? motionKind[GOBJ_SUB(self)->ctrl.motion].girlDirFrames
                            : motionKind[GOBJ_SUB(self)->ctrl.motion].dirFrames));
        }
        if (actEnemyFlagCheckDead(self) == 0) {
            ACTGame_SaveActorInformation(self);
        }
        if (sub->actMode != 0x70) {
            /* the actor-entry parameter, reloaded for the DEBUG build's state
               report */
            GObj *obj = self;
#ifdef DEBUG
            scePrintf("enemy %08x state %x\n", obj, sub->actMode);
#endif
        }
        if (sub->actMode != 0x16) {
            if (0x16 < (unsigned int)sub->actMode) {
                if (sub->actMode == 0x1C) {
                    if (0.1f < sub->stick.mag &&
                        (sub->stick.angle < -134 || 134 < sub->stick.angle)) {
                        ACTSendMailCorrect((void *)self, 0xE2);
                    } else if (0.1f < sub->stick.mag &&
                               (-45 <= sub->stick.angle && sub->stick.angle <= 45)) {
                        if ((pad[0].now & 4) == 0) {
                            ACTSendMailCorrect((void *)self, 0xC7);
                        }
                    }
                    ACTSendMailCorrect((void *)self, 0x150);
                }
            }
        }
        DispMultiBgaManagerWithKind(0x1FA, GOBJ_WORK(self)->bga, 1);
        idx = (int)GetEnemyDefParaIndex((void *)self);
        if ((unsigned int)(idx - 1) < 4) {
            _ACTParaStatus_Set(self, idx + 0x1C);
        }
        ACTParaStatus_Exec((void *)self);
        if (isEnemyActive(self) == 0 && isEnemyCarriedByGirl(self)) {
            afterCommonCarry(self);
        }
        _ACTWait(1);
    }
}

inline void actEnemyStand(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    debug_StdPrintfDummy("enter actEnemyStand\n");
    sub->actMode = 1;
    _ACTWait(0);
}

inline void motEnemyStand(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    debug_StdPrintfDummy("enter motEnemyStand\n");
    *(char **)((char *)sub + 0x130) = SetMotionRequest(self, 1, sub->env.motOriReq);
    while (1) {
        _ACTWait(1);
    }
}

inline void actEnemyWalk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    debug_StdPrintfDummy("enter actEnemyWalk\n");
    sub->actMode = 2;
    _ACTWait(0);
}

inline void motEnemyWalk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    char *mot;
    debug_StdPrintfDummy("enter motEnemyWalk\n");
    mot = SetMotionRequest(self, 8, sub->env.motOriReq);
    *(char **)((char *)sub + 0x130) = mot;
    *(int *)(mot + 0x114) = 0;
    _ACTWait(0);
}

inline void actEnemyRun(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    debug_StdPrintfDummy("enter actEnemyRun\n");
    sub->actMode = 3;
    _ACTWait(0);
}

inline void motEnemyRun(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    char *mot;
    debug_StdPrintfDummy("enter motEnemyRun\n");
    mot = SetMotionRequest(self, 0xD, sub->env.motOriReq);
    *(char **)((char *)sub + 0x130) = mot;
    *(int *)(mot + 0x114) = 0;
    _ACTWait(0);
}

inline void actEnemyJump(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    debug_StdPrintfDummy("enter actEnemyJump\n");
    sub->actMode = 4;
    _ACTWait(0);
}

void actEnemyAttack(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    int hit = 0;
    float buf[4];
    float v[4];

    _ACTWait(2);
    ACTSearchEnemy((void *)self, (int *)((char *)sub + 0x188), buf);
    _OrientXZGV(v, test_CURRENTROOT(((void *)boyGObj)), test_CURRENTROOT(self));
    sub->dir[0] = v[0];
    sub->dir[1] = v[1];
    sub->dir[2] = v[2];
    SetMotionDirection((void *)self, v);
    while (1) {
        if (GetMotionFrameFlag2((void *)self) != 0 && sub->attackTurn != 0) {
            SetMotionDirectionWithLimit((void *)self, buf, 10.0f, 90.0f);
        }
        if (sub->pad.trg & 0x80) {
            hit = 1;
        }
        if (hit != 0) {
            ACTSendMailCorrect((void *)self, 0xCD);
        }
        ACTSendMailCorrect((void *)self, 0xC7);
        EnemyAttackCenter(self);
        _ACTWait(1);
    }
}

inline void actEnemyHang(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    debug_StdPrintfDummy("enter actEnemyHang\n");
    sub->actMode = 0x1C;
    _ACTWait(0);
}

inline void funcEnemyAiGetGirl(GObj *self)
{
    Act *sub = GOBJ_ACT(self);
    if (sub->wayMode == 0) {
        sub->wayMode = 1;
    }
}

inline void actEnemyHyde(GObj *self)
{
    sceVu0FVECTOR hide = {0.0f, 0.0f, -1000000.0f};
    SetDirectRootPositionNoFitting(self, hide);
    ResetEnemyPositionInfo(self);
    actEnemyFlagOnFree(self);
}

inline int isEnemyHyde(GObj *self)
{
    GenGeo *g = &objLayout[self->labelId];
    return ((g->flags >> 21) & 1) ^ 1;
}

inline void actEnemyFlagOnFree(GObj *self)
{
    GenGeo *g = &objLayout[self->labelId];
    g->flags &= ~0x200000;
}

inline void actEnemyFlagOnDead(GObj *self)
{
    GenGeo *g = &objLayout[self->labelId];
    g->flags |= 0x40000;
}

inline int actEnemyFlagCheckDead(GObj *self)
{
    GenGeo *g = &objLayout[self->labelId];
    return (g->flags >> 18) & 1;
}

inline int isEnemyActive(GObj *self)
{
    if (self == 0 || self->kind != 4) {
        debug_assert("src/enemy_act.c", 0x827);
        __assert("src/enemy_act.c", 0x827, "ASSERTMSG__GOP_IS_NOT_ENEMY(gop)");
    }
    return actEnemyFlagCheckActive(self);
}

inline int actEnemyFlagCheckActive(GObj *self)
{
    GenGeo *g = &objLayout[self->labelId];
    unsigned int field = g->flags;
    unsigned int v0 = (field >> 18) & 1;
    if (v0 != 0)
        goto zero;
    v0 = (field >> 21) & 1;
    v0 = v0 ^ 1;
    if (v0 == 0)
        goto one;
zero:
    return 0;
one:
    return 1;
}

inline int actEnemy_isSmallEnemy(GObj *self)
{
    return GOBJ_ACT(self)->enemy->sizeClass == 0;
}

inline int actEnemy_isLargeEnemy(GObj *self)
{
    return GOBJ_ACT(self)->enemy->sizeClass == 2;
}

inline int actEnemy_isNormalEnemy(GObj *self)
{
    return GOBJ_ACT(self)->enemy->sizeClass == 1;
}

inline int actEnemy_GetClingTarget(GObj *self)
{
    Act *b = GOBJ_ACT(self);
    EnemyBattleWork *e = b->enemy;
    if (e->sizeClass == 0 && b->actMode == 0x10) {
        return e->clingTarget;
    }
    return 0;
}

/* A static inline shared by actEnemyRestart and actEnemyStart.  `max` is a
   local holding 43. */
static inline float getEnemyRestartLife(GObj *self) /* derived name */
{
    int max = 43;
    int idx = gFlagGameClear + 38;

    idx = (idx < 38) ? 38 : ((idx <= max) ? idx : max);
    return GetEnemyDefLife(self) * _ACTGame_GetParamF(idx);
}

void actEnemyRestart(GObj *self, float *pos, float *dir, int kind, GObj *mother)
{
    float v[4];
    Act *sub;
    int mail;
    int idx;
    float life;

    sub = GOBJ_ACT(self);
    mail = 50;
    v[0] = pos[0];
    v[2] = pos[2];
    v[1] = pos[1] - 100.0f;
    SetDirectRootPositionNoFitting(self, (char *)v);
    gamesysObjInfoPosSetStage(self, sub->infoPos, 0, stage_no);
    switch (kind) {
    case 0:
        pos[1] = pos[1] + GOBJ_ACT(self)->enemy->bodySize * 100.0f;
        break;
    case 1:
        mail = 51;
        break;
    case 2:
        mail = 52;
        break;
    }
    if (((int)((long long)sub->flags20.ll >> 29)) & 1) {
        *(long long *)((char *)sub + 0x20) = (long long)sub->flags20.ll & ~0x20000000;
    } else {
        RandomizeEnemy(self);
    }
    idx = 0;
    switch (GetEnemyBattleType(self)) {
    case 0:
        break;
    case 1:
        idx = 1;
        break;
    case 2:
        idx = 2;
        break;
    case 3:
        idx = 3;
        break;
    default:
        debug_assert("src/enemy_act.c", 2161);
        __assert("src/enemy_act.c", 2161, "0");
    }
    GOBJ_ACT(self)->enemy->battleType = idx;
    setBattleStatus(self);
    life = getEnemyRestartLife(self);
    sub->maxLife = life;
    sub->life = life;
    sub->wayMode = 0;
    if (mother != 0) {
        sub->mother = mother;
    } else {
        sub->mother = 0;
    }
    *(int *)((char *)sub + 0xD4) = (int)&actIntrList[79];
    ACTSendMailCorrect(self, mail);
    InitMotionGeoInfo(&GOBJ_SUB(self)->root, pos[0], pos[1], pos[2], 0.0f, 0.0f, 0.0f);
    ResetEnemyPositionInfo(self);
    SetEnemyDissolve(self, 0.0f);
    sub->restartPosX = pos[0];
    sub->restartPosY = pos[1];
    sub->restartPosZ = pos[2];
    SetMotionDirection(self, dir);
    eBrainSendMes(self, 4);
    _BrainMode_SetDirect(self, 0, 0);
}

/* PairSetGeometry is a GNU nested function, reading its parent's frame
   through the static chain. */
static int actEnemyForceSwitchToCarry(void *self)
{
    void PairSetGeometry(void *me, void *pair, float dist)
    {
        float p0[4];
        float p1[4];
        float dir[4];
        float ofs[4];

        p0[0] = test_CURRENTROOT(me)[0];
        p0[1] = test_CURRENTROOT(me)[1];
        p0[2] = test_CURRENTROOT(me)[2];
        p1[0] = test_CURRENTROOT(pair)[0];
        p1[1] = test_CURRENTROOT(pair)[1];
        p1[2] = test_CURRENTROOT(pair)[2];
        _OrientXZGV(dir, p1, p0);
        sceVu0ScaleVector(ofs, dir, dist);
        sceVu0AddVector(p1, p0, ofs);
        SetDirectRootPositionNoFitting(pair, (char *)p1);
        GOBJ_ACT(me)->dir[0] = dir[0];
        GOBJ_ACT(me)->dir[1] = dir[1];
        GOBJ_ACT(me)->dir[2] = dir[2];
        sceVu0ScaleVector((float *)((char *)GOBJ_ACT(pair) + 0x120), dir, -1.0f);
        SetMotionDirection(me, (float *)((char *)GOBJ_ACT(me) + 0x120));
        SetMotionDirection(pair, (float *)((char *)GOBJ_ACT(pair) + 0x120));
    }
    float q[4];
    Act *sub = GOBJ_ACT(self);

    if ((char *)girlGObj == 0) {
        return 0;
    }
    if (ACTReserveTarget(girlGObj, self, 0xFF) == 0) {
        return 0;
    }
    if (GOBJ_ACT(girlGObj)->actMode == 0x6F) {
        return 0;
    }
    PairSetGeometry(self, (char *)girlGObj, 50.0f);
    memset(q, 0, 0x10);
    q[3] = 1.0f;
    RotQuaternionY(q, -0x8000);
    SetMotionNodeFixModeParameter(girlGObj, self, 2, GOBJ_ACT(self)->enemy->clingNode, q, 18.0f,
                                  0.0f, 0.0f, 1.0f);
    sub->carried = girlGObj;
    GOBJ_ACT(girlGObj)->carrier = self;
    eBrainSendMes(self, 9);
    eBrainSendMes(self, 7);
    if ((60 - systemStatus[0] * 10) / systemStatus[1] * 2 < sub->frame &&
        debug_enemy_kidnap_timer != 0) {
        GOBJ_WORK(self)->basePosSet = 1;
        if ((void *)boyGObj != 0) {
            GOBJ_WORK(self)->basePos[0] = test_CURRENTROOT(((void *)boyGObj))[0];
            GOBJ_WORK(self)->basePos[1] = test_CURRENTROOT(((void *)boyGObj))[1];
            GOBJ_WORK(self)->basePos[2] = test_CURRENTROOT(((void *)boyGObj))[2];
        } else {
            GOBJ_WORK(self)->basePos[0] = test_CURRENTROOT(self)[0];
            GOBJ_WORK(self)->basePos[1] = test_CURRENTROOT(self)[1];
            GOBJ_WORK(self)->basePos[2] = test_CURRENTROOT(self)[2];
        }
    }
    return 1;
}

inline int ACTEnemyForceSwitchToCarry(GObj *self)
{
    int r = actEnemyForceSwitchToCarry(self);
    if (r != 0) {
        _BrainMode_SetDirect(self, 0, 0);
    }
    ACTSendMailCorrect(self, 0x104);
    return r;
}

inline void actEnemyNest(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    int stg;
    GObj *x2;

    GObj *x = self;
    *(int *)((char *)sub + 0x148) = 0;
    RestoreReviveCount(x);
    actChangeActBrain(isysCurrentGObj, (void *)subEnemyBrain_Idle, &sub->brainProc);
    actEnemyHyde(self);
    eBrainSendMes(self, 0xA);
    stg = stage_no;
    *(int *)((char *)sub + 0x440) = 0;
    x2 = self;
    sub->infoPos = 7;
    gamesysObjInfoPosSetStage(x2, 7, 0, stg);
    _ACTWait(0);
}

static int kidnapEndCount = 0; /* derived name: cleared as actEnemyKidnapEnd starts its wait */

void actEnemyKidnapEnd(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    float mypos[4];
    float gpos[4];

    union { /* field names derived */
        float f[4];
        int i[4];
    } q;

    float pos[4];
    float tmp[4];
    int sent = 0;
    float *p;
    int cnt = 0;
    GObj *target = 0;
    GObj *g;
    float best;
    float d;
    float dist = 0.0f;
    float ratio;
    int n;
    int r;

    if (gflagChk(393) == 0) {
        ACTGame_InsertCamera_GirlIsPinch();
    }
    g = isysGObjSearchFromObjKindID_begin(33);
    best = 10000.0f;
    mypos[0] = test_CURRENTROOT((void *)self)[0];
    mypos[1] = test_CURRENTROOT((void *)self)[1];
    mypos[2] = test_CURRENTROOT((void *)self)[2];
    while (g != 0) {
        GetRootPosition(gpos, g);
        d = _DistGV(mypos, gpos);
        if (d < best) {
            best = d;
            target = g;
        }
        g = isysGObjSearchFromObjKindID_next(g);
    }
    p = pos;
    if (target != 0) {
        memset(&q, 0, 0x10);
        q.f[3] = 1.0f;
        EntryMultiBgaManager(GOBJ_WORK(self)->bga, 0, -1, test_CURRENTROOT(target), q.f);
    }
    gflagOn(393);
    kidnapEndCount = 0;
    while (1) {
        if (((int)((long long)sub->flags20.ll >> 21)) & 1) {
            ACTGame_SetMotionPlaySpeedRatio_Reserve(self, 0.0001f, 9);
        }
        if (gflagChk(392) != 0) {
            stgmgrNextStagePreLoadForceStageSet(gFlagSaveStage);
            if ((60 - systemStatus[0] * 10) / systemStatus[1] * 3 < sub->modeFrame) {
                backStageTsuresariReturn();
                _ACTWait(0);
            }
        }
        if (5 <= sub->modeFrame) {
            if (GOBJ_ACT(girlGObj)->actMode != 0x6F || GOBJ_ACT(girlGObj)->carrier != self) {
                sub->infoPos = 0;
                gamesysObjInfoPosSetStage(self, 0, 0, stage_no);
            }
        }
        if (GetEfStageCameraTargetID() != 0) {
            ACTGame_SetMotionPlaySpeedRatio_Reserve(self, 2.0f, 0);
            if ((60 - systemStatus[0] * 10) / systemStatus[1] * 5 < sub->modeFrame) {
                goto gameover;
            }
        }
        if (GOBJ_SUB(self)->ctrl.motion == 952) {
            /* the girl's record and her action are read before the null test,
               with the carried action's id in a local */
            Act *gsub = GOBJ_ACT(girlGObj);
            int act = gsub->actMode;
            int carriedAct = 0x6F;

            if ((char *)girlGObj == 0 || act != carriedAct || gsub->carrier != self) {
                ratio = dist / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
                SetEnemyDissolve(self, (ratio < 0.01) ? 0.01f : ((1.0f < ratio) ? 1.0f : ratio));
                dist = dist + 1.0f;
            }
            if (50.0f < GOBJ_SUB(self)->ctrl.animFrame) {
                if ((((int)((long long)sub->flags20.ll >> 21)) & 1) == 0) {
                gameover:
                    if (GOBJ_ACT(girlGObj)->actMode == 0x6F &&
                        GOBJ_ACT(girlGObj)->carrier == self) {
                        if (target != 0) {
                            q.i[0] = (int)target;
                            best = 0.0f;
                            q.i[1] = 0;
                            if ((void *)boyGObj != 0 && (char *)girlGObj != 0) {
                                GetRootPosition(pos, (void *)boyGObj);
                                GetRootPosition(tmp, girlGObj);
                                best = GetPointDistance(pos, tmp) + 1000.0f;
                                debug_StdPrintfDummy("radius: %f\n", best);
                            }
                            gameover_flag = 1;
                            if ((char *)girlGObj != 0) {
                                *(int *)((char *)girlGObj + 0x16C) = 0;
                            }
                            stage_SetParentOfGObj(502, &q);
                            n = (GetEfStageCameraTargetID() != 0) ? 120 : 300;
                            r = n * ((60 - systemStatus[0] * 10) / systemStatus[1]) / 60;
                            StartGameOverEffect(test_CURRENTROOT(target),
                                                (best < (float)(r * 50)) ? 50.0f : best / (float)r);
                            _ACTRun(r);
                        }
                        ACT_LAYOUT_GAMEOVER();
                        _ACTWait(0);
                    } else {
                        ACTSendMailCorrect(self, 353);
                    }
                }
            }
        }
        if ((void *)boyGObj != 0) {
            if (GOBJ_ACT(boyGObj)->actMode == 0x6D) {
                _OrientXZGV(q.f, test_CURRENTROOT((void *)boyGObj), test_CURRENTROOT((void *)self));
                sceVu0ScaleVector(q.f, q.f, -1.0f);
                SetMotionDirectionSmooze(self, q.f, 3.0f);
                ACTSendMailCorrect(self, 0x167);
                ACTSendMailCorrect(self, 0x168);
                ACTSendMailCorrect(self, 0x169);
                ACTSendMailCorrect(self, 0x16A);
            }
        }
        if (target != 0) {
            pos[0] = test_CURRENTROOT(target)[0];
            pos[1] = test_CURRENTROOT(target)[1];
            pos[2] = test_CURRENTROOT(target)[2];
            pos[1] = test_CURRENTROOT((void *)self)[1];
            _InterGV(tmp, p, test_CURRENTROOT((void *)self), 5.0f, 1.0f);
            SetRootPosition(self, tmp);
        }
        ACTSendMailCorrect(self, 0x16B);
        if ((char *)girlGObj == 0 || GOBJ_ACT(girlGObj)->actMode != 0x6F ||
            GOBJ_ACT(girlGObj)->carrier != self) {
            if ((60 - systemStatus[0] * 10) / systemStatus[1] / 6 < ++cnt && sent == 0) {
                eBrainSendMes(self, 10);
                sent = 1;
            }
        }
        _ACTWait(1);
    }
}

/* A static inline inside actEnemyKidnapBegin, the same construction as
   enemyPickupCheckGirl above.  pos, declared ahead of buf, is read only by
   the DEBUG build's report of the girl's position. */
static inline int enemyKidnapCheckGirl(GObj *self) /* derived name */
{
    float pos[4];
    float buf[4];
    int ang;
    int mode;

    if (_ACTGame_SearchGObj(self, girlGObj, 60.0f, 100.0f, 45, buf) != 0) {
        ang = _RotyGV(buf, test_CURRENTORIENT((girlGObj)));
        ang = (ang < 0) ? -ang : ang;
        mode = 2;
        if (ang <= 89) {
            mode = 1;
        }
    } else {
        mode = 0;
    }
#ifdef DEBUG
    GetRootProjectionPosOfGObj(pos, (char *)((char *)girlGObj));
    scePrintf("kidnap check %d girl %f %f %f\n", mode, pos[0], pos[1], pos[2]);
#endif
    return mode;
}

void actEnemyKidnapBegin(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    float *dir = (float *)((char *)sub + 0x120);
    int mail = 0x163;
    int mode;

    while (1) {
        if (GOBJ_SUB(self)->ctrl.motion == 0x3AA) {
            _OrientXZGV(dir, test_CURRENTROOT((girlGObj)), test_CURRENTROOT(self));
            if (0.1f < sub->stick.mag && sub->actMode != 0x73) {
                SetMotionDirectionSmooze(
                    (void *)self, dir,
                    (float)((self == girlGObj && girlControlMode != 0)
                                ? motionKind[GOBJ_SUB(self)->ctrl.motion].girlDirFrames
                                : motionKind[GOBJ_SUB(self)->ctrl.motion].dirFrames));
            }
            mode = enemyKidnapCheckGirl(self);
            switch (mode) {
            case 1:
                mail = 0x164;
                /* fallthrough */
            case 2:
                if (actEnemyForceSwitchToCarry((void *)self) != 0) {
                    if (mode == 1) {
                        sceVu0ScaleVector((float *)((char *)GOBJ_ACT(girlGObj) + 0x120),
                                          (float *)((char *)GOBJ_ACT(girlGObj) + 0x120), -1.0f);
                        SetMotionDirection((void *)((char *)girlGObj),
                                           (float *)((char *)GOBJ_ACT(girlGObj) + 0x120));
                    }
                    ACTGame_InsertCamera_GirlIsPinch();
                    while (1) {
                        ACTSendMailCorrect((void *)self, mail);
                        _ACTWait(1);
                    }
                }
                break;
            }
            ACTSendMailCorrect((void *)self, 0x165);
        }
        _ACTWait(1);
    }
}

static void MoveChestForCatchBoy(GObj *self)
{
    float p0[4];
    float p1[4];
    float sk[4];
    float ori[4];
    float d[4];
    float sc[4];
    float t;
    float b;
    float a;
    int ang;
    int ang2;

    GOBJ_SUB(self)->ctrl.catchBoy = 1;
    GOBJ_SUB(self)->root.lookMode = 2;
    GetRootProjectionPosOfGObj(p0, self);
    GetRootProjectionPosOfGObj(p1, ((void *)boyGObj));
    GetSkeltonPosition(sk, self, 1);
    t = (p0[1] - p1[1]) / 600.0f;
    t = (t < 0.0f) ? 0.0f : ((1.0f < t) ? 1.0f : t);
    a = t * 1000.0f + -200.0f;
    b = t * -400.0f;
    ori[0] = test_CURRENTORIENT(self)[0];
    ori[1] = test_CURRENTORIENT(self)[1];
    ori[2] = test_CURRENTORIENT(self)[2];
    _OrientXZGV(d, p1, p0);
    ang = _RotyGV(ori, (void *)d);
    if (-45 <= ang) {
        if (45 < ang) {
            ang2 = 45;
        } else {
            ang2 = ang;
        }
    } else {
        ang2 = -45;
    }
    _ApplyRyGV(ori, (float)ang2 * 3.1415927f / 180.0f);
    sceVu0ScaleVector(sc, ori, a);
    sc[1] = b;
    sceVu0AddVector((float *)((char *)GOBJ_SUB(self) + 0x390), p0, sc);
    debug_NMarker((float *)((char *)GOBJ_SUB(self) + 0x390), 255, 0, 0, 200.0f);
}

inline void afterEnemyBodylift(GObj *volatile self)
{
    GObj *x = self;
    GOBJ_SUB(x)->ctrl.catchBoy = 0;
    GOBJ_SUB(x)->root.lookMode = 0;
}

/* a `static inline` defined outside this function */
static inline void enemyBodyliftClearBoy(char *self) /* derived name */
{
    GOBJ_SUB(self)->ctrl.catchBoy = 0;
    GOBJ_SUB(self)->root.lookMode = 0;
}

void actEnemyBodylift(GObj *volatile self)
{
    float dir[4];
    float pos[4];
    float bpos[4];
    float mtx[16];
    float lv[4];
    Act *sub;
    int hit;

    sub = GOBJ_ACT(self);
    hit = 0;
    GOBJ_ACT(self)->enemy->flags.ll &= ~4LL;
    _OrientXZGV(dir, bodyliftTarget[0], test_CURRENTROOT(self));
    sub->after = (void *)afterEnemyBodylift;
    if (GOBJ_ACT(self)->enemy->liftKind != 3) {
        _OrientXZGV(sub->dir, test_CURRENTROOT(((void *)boyGObj)), test_CURRENTROOT(self));
        SetMotionDirection((void *)self, sub->dir);
    }
    for (;;) {
        GOBJ_ACT(self)->enemy->flags.ll &= ~4LL;
        if (GOBJ_ACT(self)->enemy->liftKind == 3) {
            if (GOBJ_ACT(boyGObj)->actMode == 94) {
                enemyBodyliftClearBoy((char *)self);
            } else {
                MoveChestForCatchBoy(self);
            }
        }
        GetSkeltonPosition(pos, self, 22);
        bpos[0] = test_CURRENTROOT(((void *)boyGObj))[0];
        bpos[1] = test_CURRENTROOT(((void *)boyGObj))[1];
        bpos[2] = test_CURRENTROOT(((void *)boyGObj))[2];
        if (GOBJ_ACT(self)->enemy->liftKind == 3) {
            sceVu0SubVector(lv, test_CURRENTROOT(((void *)boyGObj)), pos);
            GetMatrixDirectionToZ(mtx, test_CURRENTORIENT(self));
            lv[3] = 0.0f;
            sceVu0ApplyMatrix(lv, mtx, lv);
            if (((lv[0] < 0.0f) ? -lv[0] : lv[0]) < 100.0f &&
                ((lv[1] < 0.0f) ? -lv[1] : lv[1]) < 100.0f && -300.0f < lv[2] && lv[2] < 200.0f) {
                hit = 1;
            }
        } else {
            if (_DistSqGV(pos, bpos) < GOBJ_ACT(self)->enemy->bodySize * 45.0f *
                                           (GOBJ_ACT(self)->enemy->bodySize * 45.0f)) {
                hit = 1;
            }
        }
        /* a volatile read of the actor-entry home whose value nothing
           consumes: what is left of a compiled-out statement */
        (void)self;
        if (GetMotionFrameFlag1((void *)self) != 0 && hit != 0) {
            iosOmSendMail(((void *)boyGObj), 0x170, self);
        }
        if (GetMotionFrameFlag2((void *)self) != 0) {
            _OrientXZGV(bpos, test_CURRENTROOT(((void *)boyGObj)), test_CURRENTROOT(self));
            _ACTMotDirSmzDirect((void *)self, bpos);
        }
        if (GOBJ_ACT(boyGObj)->actMode == 94 && GOBJ_ACT(boyGObj)->enemy->liftedObj == (int)self) {
            if (GOBJ_ACT(self)->enemy->liftKind == 3) {
                if (0 < GOBJ_ACT(boyGObj)->enemy->liftLevel) {
                    ACTSendMailCorrect((void *)self, 0x176);
                } else {
                    ACTSendMailCorrect((void *)self, 0x177);
                }
            } else {
                ACTSendMailCorrect((void *)self, 0x174);
            }
        } else {
            ACTSendMailCorrect((void *)self, 0xC7);
        }
        _ACTWait(1);
    }
}

inline void actEnemyBodyslamFail(GObj *volatile self)
{
    iosOmSendMail(((void *)boyGObj), 0xE2, self);
    while (1) {
        ACTSendMailCorrect((void *)self, 0xC7);
        _ACTWait(1);
    }
}

inline void actEnemyBodyslam(GObj *volatile self)
{
    iosOmSendMail(((void *)boyGObj), GOBJ_ACT(self)->enemy->bodyslamMail, self);
    while (1) {
        ACTSendMailCorrect((void *)self, 0xC7);
        _ACTWait(1);
    }
}

/* A static inline inside actEnemyPickupBegin.  A second 16-byte vector is
   declared ahead of buf; its reader is the DEBUG build's report of the girl's
   position, as in enemyKidnapCheckGirl. */
static inline int enemyPickupCheckGirl(GObj *self) /* derived name */
{
    float pos[4];
    float buf[4];
    int ang;
    int mode;

    if (_ACTGame_SearchGObj(self, girlGObj, 170.0f, 100.0f, 45, buf) != 0) {
        ang = _RotyGV(buf, test_CURRENTORIENT((girlGObj)));
        ang = (ang < 0) ? -ang : ang;
        mode = 2;
        if (ang <= 89) {
            mode = 1;
        }
    } else {
        mode = 0;
    }
#ifdef DEBUG
    GetRootProjectionPosOfGObj(pos, (char *)((char *)girlGObj));
    scePrintf("pickup check %d girl %f %f %f\n", mode, pos[0], pos[1], pos[2]);
#endif
    return mode;
}

void actEnemyPickupBegin(GObj *volatile self)
{
    float *dir = (float *)((char *)GOBJ_ACT(self) + 0x120);
    float *girl = test_CURRENTROOT((girlGObj));
    float *me = test_CURRENTROOT(self);
    int mode;

    _OrientXZGV(dir, girl, me);
    SetMotionDirection((void *)self, dir);
    while (1) {
        mode = enemyPickupCheckGirl(self);
        if (mode < 3 && mode != 0) {
            if (actEnemyForceSwitchToCarry((void *)self) != 0) {
                ACTGame_InsertCamera_GirlIsPinch();
                while (1) {
                    ACTSendMailCorrect((void *)self, 0x16D);
                    _ACTWait(1);
                }
            }
        }
        ACTSendMailCorrect((void *)self, 0x16E);
        _ACTWait(1);
    }
}

inline void actEnemyCarry(GObj *volatile self)
{
    debug_assert("src/enemy_act.c", 0xB75);
    __assert("src/enemy_act.c", 0xB75, "0");
}

inline int EnemyBrainStatus_Boy(GObj *self)
{
    return GOBJ_ACT(self)->brainAim == 2;
}

inline int EnemyBrainStatus_Girl(GObj *self)
{
    return GOBJ_ACT(self)->brainAim == 1;
}

/* a static inline */
static inline int getEnemyBrainMes(char *self, int *data) /* derived name */
{
    EBSlot *t = eBrainGetTarget(self);

    if (t == 0) {
        *data = 0;
        return 0;
    }
    *data = (int)t->target;
    return t->status;
}

static void CheckEnemyBrainMode(char *self, int *outMode, int *outData)
{
    char *sub = *(char **)(self + 0x164);
    int mode;

    *outData = 0;
    if (*(int *)(sub + 0x148) != 0 && motionKind[GOBJ_SUB(self)->ctrl.motion].word100 == 0) {
        *outMode = -1;
        return;
    }
    if (((*(unsigned long long *)(sub + 0x18) >> 49) & 1) == 0) {
        *outMode = -1;
        return;
    }
    if (actEnemyFlagCheckActive((GObj *)self) == 0) {
        *outMode = -1;
        return;
    }
    switch (*(unsigned int *)(sub + 0x34)) {
    case 7:
    case 19:
    case 20:
    case 21:
    case 22:
    case 114:
    case 115:
        *outMode = -1;
        return;
    case 103:
        if ((char *)girlGObj == 0) {
            *outMode = -1;
            return;
        }
        if (GOBJ_ACT(girlGObj)->actMode != 0x6F) {
            *outMode = -1;
            return;
        }
        if ((char *)GOBJ_ACT(girlGObj)->carrier != self) {
            *outMode = -1;
            return;
        }
        break;
    }
    if (((*(unsigned long long *)(sub + 0x20) >> 34) & 1) == 0) {
        goto no_bit;
    }
    *(unsigned long long *)(sub + 0x20) &= ~(1ULL << 34);
    mode = -2;
    goto store;
no_bit:
    mode = getEnemyBrainMes(self, outData);
store:
    *outMode = mode;
}

inline void _BrainMode_SetDirect(GObj *self, int mode, BrainModeTarget *tgt)
{
    GOBJ_ACT(self)->enemy->reqMode = mode;
    if (tgt != 0) {
        GOBJ_ACT(self)->enemy->flags.w.reqTarget = *tgt;
    } else {
        GOBJ_ACT(self)->enemy->flags.w.reqTarget = brainTargetNone;
    }
}

/* A static inline between _BrainMode_SetDirect and subEnemyBrainMain. */
static inline void _BrainMode_Set(GObj *self, int mode, int *tgt) /* derived name */
{
    if (brainModeTable[mode].pri < brainModeTable[GOBJ_ACT(self)->enemy->reqMode].pri) {
        return;
    }
    _BrainMode_SetDirect(self, mode, tgt);
}

void subEnemyBrainMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    int mode;
    int data;
    int i;

    /* BrainMode_Requset is a GNU nested function: it reads self out of
       subEnemyBrainMain's frame through the static chain. */
    void BrainMode_Requset(int req, int arg)
    {
        switch (req) {
        case 0:
            _BrainMode_Set(self, 1, 0);
            break;
        case -2:
            _BrainMode_Set(self, 1, 0);
            break;
        case 1:
            _BrainMode_Set(self, 3, 0);
            break;
        case 2:
            _BrainMode_Set(self, 4, 0);
            break;
        case 3:
            _BrainMode_Set(self, 6, 0);
            break;
        case -3:
        case -1:
        case 7:
            _BrainMode_Set(self, 13, 0);
            break;
        case 5:
            _BrainMode_Set(self, 7, &arg);
            break;
        case 4:
        case 6:
            _BrainMode_Set(self, 5, &arg);
            break;
        case 8:
            _BrainMode_SetDirect(self, 2, &arg);
            break;
        default:
            debug_StdPrintfDummy("undefined mode [%d]\n", req);
            debug_assert("src/enemy_act.c", 3102);
            __assert("src/enemy_act.c", 3102, "0");
            break;
        }
    }

    GOBJ_ACT(self)->enemy->reqMode = GOBJ_ACT(self)->enemy->mode = 0;
    GOBJ_ACT(self)->enemy->waitCount = 2;
    _ACTWait(1);
    _ACTWait(1);
    _ACTWait(1);
    switch (sub->brainStatus) {
    case 4:
        if ((char *)girlGObj != 0) {
            eBrainSendMes(self, 9);
            i = 0;
            eBrainSendMes(self, 7);
            actEnemyForceSwitchToCarry((void *)self);
            for (; i < 5; i++) {
                if (sub->actMode == 5) {
                    ACTReserveTarget(girlGObj, (void *)self, 0xFF);
                    *(char **)((char *)sub + 0x130) =
                        SetMotionRequest(self, 0x109, sub->env.motOriReq);
                } else {
                    *(char **)((char *)sub + 0x130) =
                        SetMotionRequest(self, 0x107, sub->env.motOriReq);
                }
                if (*(int *)((char *)sub->motReq + 0xC) != 0) {
                    break;
                }
                _ACTWait(1);
            }
            if (gflagChk(0x189) != 0) {
                *(char **)((char *)sub + 0x130) = SetMotionRequest(self, 0x108, sub->env.motOriReq);
            }
        }
        break;
    case 1:
        if ((char *)girlGObj != 0 &&
            ACTCheckViewCl((void *)self, (char *)girlGObj, test_CURRENTROOT((girlGObj)), 0x168,
                           3.40282347e+38f /* FLT_MAX */) != 0) {
            eBrainSendMes(self, 2);
        } else {
            eBrainSendMes(self, 1);
        }
        break;
    case 2:
        if ((char *)girlGObj != 0) {
            eBrainSendMes(self, 2);
        }
        break;
    case 5:
        eBrainSendMes(self, 3);
        break;
    }
    while (1) {
        if ((char *)girlGObj != 0 && GOBJ_ACT(girlGObj)->actMode == 0x6F &&
            GOBJ_ACT(girlGObj)->carrier == self && EA_CHKBIT(GOBJ_WORK(self)->enemyFlags, 0)) {
            ACTSendMailCorrect((void *)self, 0x1E);
            ACTSendMailCorrect((void *)self, 0x1D);
        }
        CheckEnemyBrainMode((char *)self, &mode, &data);
        BrainMode_Requset(mode, data);
        if (GOBJ_ACT(self)->enemy->reqMode != GOBJ_ACT(self)->enemy->mode ||
            (((int)(sub->flags20.ll >> 9)) & 1) != 0) {
            sub->flags20.ll &= ~0x200LL;
            GOBJ_ACT(self)->enemy->mode = GOBJ_ACT(self)->enemy->reqMode;
            GOBJ_ACT(self)->enemy->target = GOBJ_ACT(self)->enemy->flags.w.reqTarget.gobj;
            sub->brainTarget = (GObj *)GOBJ_ACT(self)->enemy->target;
            sub->brainAim = brainModeTable[GOBJ_ACT(self)->enemy->mode].aim;
            sub->infoPos = brainModeTable[GOBJ_ACT(self)->enemy->mode].infoPos;
            if (sub->infoPos == 4) {
                gamesysObjInfoPosSetStage(self, 4, 0, stage_no);
            }
            actChangeActBrain(isysCurrentGObj,
                              (void *)brainModeTable[GOBJ_ACT(self)->enemy->mode].brain,
                              &sub->brainProc);
        }
        if (sub->carried != 0) {
            _ACTCharStatus_Set((void *)self, 0x10, -1.0f, 0);
        }
        if (GOBJ_ACT(self)->enemy->waitCount != 0) {
            GOBJ_ACT(self)->enemy->waitCount -= 1;
        }
        if ((((int)((long long)sub->flags20.ll >> 6)) & 1) != 0) {
            char *g = *(char **)(char *)sub;

            if (sub->actMode != 0x67) {
                SetEnemyStonizedVisual((void *)self);
            }
            *(long long *)((char *)sub + 0x20) |= 0x200000LL;
            sub->stick.mag = 0;
            *(int *)((char *)sub + 0x120) = 0;
            *(int *)((char *)sub + 0x124) = 0;
            *(int *)((char *)sub + 0x128) = 0;
            sub->stick.y = 127;
            sub->stick.x = 127;
            isysGObjProcPause(g);
            while (1) {
                _ACTWait(1);
            }
        }
        if (brainModeTable[GOBJ_ACT(self)->enemy->mode].stoppable != 0 &&
            (((int)((long long)sub->flags20.ll >> 5)) & 1) != 0) {
            char *g = *(char **)(char *)sub;

            isysGObjProcPause(g);
            *(long long *)((char *)sub + 0x20) |= 0x200000LL;
            sub->stick.mag = 0;
            *(int *)((char *)sub + 0x120) = 0;
            *(int *)((char *)sub + 0x124) = 0;
            *(int *)((char *)sub + 0x128) = 0;
            sub->stick.y = 127;
            sub->stick.x = 127;
            while (1) {
                if ((char *)girlGObj == 0 || GOBJ_ACT(girlGObj)->actMode != 0x6F ||
                    GOBJ_ACT(girlGObj)->carrier != self) {
                    if (actModeTbl[GOBJ_ACT(self)->actMode].bit5) {
                        ACTSendMailCorrect((void *)self, 0x102);
                    }
                }
                if ((((int)((long long)sub->flags20.ll >> 4)) & 1) != 0) {
                    isysGObjProcActive(g);
                    *(long long *)((char *)sub + 0x20) &= ~0x200000LL;
                    break;
                }
                _ACTWait(1);
            }
        }
        BossEnemyFunc((void *)self);
        _ACTWait(1);
    }
}

inline void afterCommonCarry(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    GObj *girl = girlGObj;
    GObj *obj = self;
    sub->carried = girl;
    iosOmSendMail(girl, 0x30, obj);
    sub->carried = 0;
    if (sub->actMode == 5) {
        eBrainSendMes(self, 4);
    }
}

inline void funcEnemyCarryFail(GObj *self)
{
    GOBJ_ACT(self)->flags20.ll |= (1ULL << 34);
}

inline void subEnemyBrain_Idle(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    sub->stick.mag = 0;
    *(int *)((char *)sub + 0x120) = 0;
    *(int *)((char *)sub + 0x124) = 0;
    *(int *)((char *)sub + 0x128) = 0;
    while (1) {
        if (GOBJ_ACT(self)->enemy->target == (int)((void *)boyGObj)) {
            _DoAwait(self);
        }
        _ACTWait(1);
    }
}

inline void subEnemyBrain_Await(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    sub->stick.mag = 0;
    *(int *)((char *)sub + 0x120) = 0;
    *(int *)((char *)sub + 0x124) = 0;
    *(int *)((char *)sub + 0x128) = 0;
    if ((void *)boyGObj != 0) {
        _ApproachTarget(self, (void *)boyGObj, (char *)sub + 0x120, 0,
                        (float)((int)(_GetRandom() * 10.0f) % 200 + 300), 0);
    }
    sub->stick.mag = 0;
    *(int *)((char *)sub + 0x120) = 0;
    *(int *)((char *)sub + 0x124) = 0;
    *(int *)((char *)sub + 0x128) = 0;
    while (1) {
        _DoAwait(self);
        _ACTWait(1);
    }
}

inline void subEnemyBrain_FindGirl(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    int i;

    for (i = 0; i < (60 - systemStatus[0] * 10) / systemStatus[1] / 2; i++) {
        *(int *)((char *)sub + 0x34C) = 0;
        *(int *)((char *)sub + 0x120) = 0;
        *(int *)((char *)sub + 0x124) = 0;
        *(int *)((char *)sub + 0x128) = 0;
        ACTSendMailCorrect((void *)self, 0xE6);
        if (sub->actMode == 0x47) {
            break;
        }
        _ACTWait(1);
    }
    for (i = 0; i < (60 - systemStatus[0] * 10) / systemStatus[1] * 250 / 60; i++) {
        _DoAwait(self);
        _ACTWait(1);
    }
    eBrainSendMes(self, 1);
    _ACTWait(0);
}

void subEnemyBrain_ToGenerator(GObj *self)
{
    /* The actor handle is kept in a `volatile` local: this brain thread is
       resumed by the actor scheduler at every _ACTWait, so the frame slot,
       not a register, is the live copy of the handle. */
    GObj *volatile gobj = self;
    Act *sub = GOBJ_ACT(gobj);
    GObj *target = sub->brainTarget;

    GOBJ_WORK(gobj)->genTarget = target;
    SetKidnapInfo(-1, -1);
    if (GOBJ_WORK(gobj)->basePosSet != 0) {
        float best = 0.0f;
        GObj *g;

        for (g = isysGObjSearchFromObjKindID_begin(33); g != 0;
             g = isysGObjSearchFromObjKindID_next(g)) {
            if (IsOpenGenerator(g) != 0) {
                float d;

                d = _DistSqGV(GOBJ_WORK(gobj)->basePos, test_CURRENTROOT(g));
                if (best < d) {
                    best = d;
                    GOBJ_WORK(gobj)->genTarget = g;
                    sub->brainTarget = g;
                    target = g;
                    SetKidnapInfo(gobj->labelId, target->labelId);
                }
            }
        }
    }
    if ((unsigned char)_ApproachTarget(gobj, target, (char *)sub + 0x120, 0, 50.0f,
                                       *(unsigned char *)((char *)GOBJ_ACT(gobj)->enemy + 0x224)) ==
        0) {
        debug_StdPrintfDummy("to generator way error!");
        sub->stick.mag = 0;
        *(int *)((char *)sub + 0x120) = 0;
        *(int *)((char *)sub + 0x124) = 0;
        *(int *)((char *)sub + 0x128) = 0;
        _ACTWait(30);
        ACTSendMailCorrect((void *)gobj, 0x100);
        _ACTWait(0);
    }
    while (1) {
        ACTSendMailCorrect((void *)gobj, 0x166);
        _ACTWait(1);
    }
}

/* A static inline.  `sub` is computed inside the helper; in enemy_dodge the
   caller already holds it. */
static inline void enemyDodgeSendMail(GObj *self) /* derived name */
{
    Act *sub = GOBJ_ACT(self);

    if (EnemyUtil_isOtherStatus(self, 0) != 0) {
        return;
    }
    if (((int)(_GetRandom() * 10.0f)) & 1) {
        ACTSendMailCorrect(self, 0xCF);
    }
    ACTSendMailCorrect(self, 0xCD);
    *(long long *)((char *)sub + 0x20) |= 0x400;
}

static void enemy_dodge(GObj *self)
{
    float a[4];
    float b[4];
    float c[4];
    GObj *boy = boyGObj;
    Act *sub;
    float d;
    int ang;

    if (boy == 0) {
        return;
    }
    d = _DistGV(test_CURRENTROOT(boy), test_CURRENTROOT(self));
    if (d < GetEnemyDefDodgeRange(self)) {
        GetRootPosition(a, self);
        GetRootPosition(b, boy);
        sceVu0SubVector(c, b, a);
        sceVu0Normalize(c, c);
        ang = _RotyGV(c, test_CURRENTORIENT(boy));
        ang = (ang < 0) ? -ang : ang;
        if (ang < 114) {
            return;
        }
        ang = _RotyGV(c, test_CURRENTORIENT(self));
        ang = (ang < 0) ? -ang : ang;
        if ((float)ang < 45.0f) {
            if (IsBoyStatus_NotDanger() != 0) {
                return;
            }
            sub = GOBJ_ACT(self);
            if ((((int)(*(long long *)((char *)sub->enemy + 0x210) >> 1)) & 1) == 0) {
                if (d < 200.0f) {
                    enemyDodgeSendMail(self);
                }
            } else if (debug_ignore_dodge == 0) {
                ACTSendMailCorrect(self, 0x113);
            }
        }
    }
}

static void enemy_dodge_to_boy(GObj *self)
{
    float boy[4];
    float me[4];
    float v[4];
    int ang;

    if ((void *)boyGObj == 0) {
        return;
    }
    if (((int)(*(long long *)((char *)GOBJ_ACT(self)->enemy + 0x210) >> 1)) & 1) {
        boy[0] = test_CURRENTROOT(((void *)boyGObj))[0];
        boy[1] = test_CURRENTROOT(((void *)boyGObj))[1];
        boy[2] = test_CURRENTROOT(((void *)boyGObj))[2];
        me[0] = test_CURRENTROOT(self)[0];
        me[1] = test_CURRENTROOT(self)[1];
        me[2] = test_CURRENTROOT(self)[2];
        if (_DistSqGV(boy, me) < GetEnemyDefDodgeRange(self) * GetEnemyDefDodgeRange(self)) {
            _OrientXZGV(v, boy, me);
            ang = _RotyGV(v, test_CURRENTORIENT(((void *)boyGObj)));
            ang = (ang < 0) ? -ang : ang;
            if (ang < 114) {
                return;
            }
            ang = _RotyGV(v, test_CURRENTORIENT(self));
            ang = (ang < 0) ? -ang : ang;
            if ((float)ang < 45.0f) {
                if (IsBoyStatus_NotDanger() != 0) {
                    return;
                }
                if (debug_ignore_dodge != 0) {
                    return;
                }
                ACTSendMailCorrect(self, 0x113);
            }
        }
    }
}

/* a `static inline` defined outside this function, expanded twice here */
static inline float battleRangeScale(GObj *self, float v) /* derived name */
{
    EnemyBattleWork *work = GOBJ_ACT(self)->enemy;

    switch (work->sizeClass) {
    case 0:
    case 1:
        v = work->bodySize * v;
        if (((int)(*(long long *)((char *)work + 0x210) >> 1)) & 1) {
            v = v * 1.2;
        }
        break;
    case 2:
        v = work->bodySize * 0.7f * v;
        break;
    }
    return v;
}

static int Battle_isCurrentStatus(GObj *self, GObj *tgt, float *pos)
{
    float ori[4];
    float dir[4];
    int ret;
    float xz;
    float dy;
    float range;
    float vflag;
    float a;
    float b;
    int ang;
    int far;

    ret = 0;
    xz = _DistxzGV(pos, test_CURRENTROOT(tgt));
    dy = ((pos[1] - test_CURRENTROOT(tgt)[1]) < 0.0f) ? -(pos[1] - test_CURRENTROOT(tgt)[1])
                                                      : (pos[1] - test_CURRENTROOT(tgt)[1]);
    vflag = 0.0f;
    range = GetEnemyDefDodgeRange(self);
    if (200.0f < xz || battleRangeScale(self, 200.0f) < dy) {
        vflag = 1.0f;
    }
    if (xz < range && dy < battleRangeScale(self, 150.0f)) {
        vflag = -1.0f;
    }
    ori[0] = test_CURRENTORIENT(tgt)[0];
    ori[1] = test_CURRENTORIENT(tgt)[1];
    ori[2] = test_CURRENTORIENT(tgt)[2];
    _OrientXZGV(dir, test_CURRENTROOT(self), test_CURRENTROOT(tgt));
    ang = _AbsRotyGV(ori, dir);
    a = (ang < 75) ? 1.0f : 0.0f;
    far = (101 <= ang);
    b = (a != 0.0f && GOBJ_ACT(tgt)->actMode == 15) ? 1.0f : 0.0f;
    if (vflag < 0.0f) {
        ret = 1;
    }
    if (0.0f < vflag) {
        ret = 2;
    }
    if (b != 0.0f) {
        ret = 3;
    }
    if (vflag <= 0.0f) {
        ret = far ? 4 : ret;
    }
    return ret;
}

inline void EnemyUtil_TurnToBoy(GObj *self, GObj *tgt, int smooze)
{
    float dir[4];
    Act *sub = GOBJ_ACT(self);

    _OrientXZGV(dir, test_CURRENTROOT(tgt), test_CURRENTROOT(self));
    sub->dir[0] = dir[0];
    sub->dir[1] = dir[1];
    sub->dir[2] = dir[2];
    enemyCheckTurnAngle(self);
    if (smooze == 0) {
        SetMotionDirection(self, dir);
    } else {
        SetMotionDirectionSmooze(self, dir, (float)smooze);
    }
}

inline int EnemyUtil_isOtherStatus(GObj *self, int mode)
{
    GObj *g;
    for (g = isysGObjSearchFromObjKindID_begin(4); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        if (g != self) {
            Act *sub = GOBJ_ACT(g);
            if (sub->actMode == 0xF) {
                return (int)g;
            }
            if ((int)((long long)sub->flags20.ll >> 10) & 1) {
                return (int)g;
            }
        }
    }
    return 0;
}

static int GetFlyPosition(float *out, float *me, float *tgt)
{
    int ret;

    ret = 0;
    if (tgt[1] < -500.0f && 1000.0f < ((tgt[2] < 0.0f) ? -tgt[2] : tgt[2]) && -500.0f < me[1]) {
        out[0] = flyEscapePos[0];
        out[1] = flyEscapePos[1];
        out[2] = flyEscapePos[2];
        return 2;
    }
    if (tgt[1] < -500.0f && 1000.0f < ((tgt[2] < 0.0f) ? -tgt[2] : tgt[2]) &&
        _DistSqGV(me, tgt) < 160000.0f) {
        out[0] = flyEscapePos[0];
        out[1] = flyEscapePos[1];
        out[2] = flyEscapePos[2];
        return 2;
    }
    if (-150.0f < me[1]) {
        float best = 3.40282347e+38f /* FLT_MAX */;
        int besti = -1;
        int i;

        for (i = 0; i < 4; i++) {
            float d = _DistSqGV(me, flyCheckPos[i]);

            if (d < best) {
                best = d;
                besti = i;
            }
        }
        if (besti != -1) {
            float *p = flyDestPos[besti];

            ret = 1;
            out[0] = p[0];
            out[1] = p[1];
            out[2] = p[2];
        }
    } else {
        float best = 0.0f;
        int besti = -1;
        int i;

        for (i = 0; i < 4; i++) {
            float d = _DistSqGV(tgt, flyCheckPos[i]);

            if (best < d) {
                best = d;
                besti = i;
            }
        }
        if (besti != -1) {
            ret = 1;
            if (((int)(_GetRandom() * 10.0f)) & 1) {
                out[0] = flyEscapePos[0];
                out[1] = flyEscapePos[1];
                out[2] = flyEscapePos[2];
            } else {
                /* the table base is its own statement */
                float *tbl = flyCheckPos[0];
                float *p = tbl + besti * 4;

                out[0] = p[0];
                out[1] = p[1];
                out[2] = p[2];
            }
        }
    }
    return ret;
}

/* An _ApproachTarget callback: _ApproachTarget_Way calls its `fn` through
   (void (*)(char *, void *, float)).  The float is unused here; the body
   measures its own `dist`. */
static void NakaBoss(GObj *self, void *tgt, float distArg)
{
    float bpos[4];
    float mpos[4];
    float ori[4];
    float dir[4];
    GObj *boy = boyGObj;
    int inc = 0;
    int half = (60 - systemStatus[0] * 10) / systemStatus[1] / 4;
    float dist;
    Act *sub;

    if (stage_no != 86 && stage_no != 3 && stage_no != 46) {
        if (tgt != 0) {
            enemy_dodge_to_boy(self);
        }
        return;
    }
    {
        if (boy == 0) {
            return;
        }
        dist = _DistGV(test_CURRENTROOT(boy), test_CURRENTROOT(self));
        if (GetFlyPosition((float *)((char *)GOBJ_ACT(self)->work + 0x8A0), test_CURRENTROOT(self),
                           test_CURRENTROOT(boy)) == 2) {
            ACTSendMailCorrect(self, 0x1D);
        }
        if (dist < 360.0) {
            GetRootPosition(bpos, boy);
            GetRootPosition(mpos, self);
            _OrientXZGV(dir, mpos, bpos);
            ori[0] = test_CURRENTORIENT(boy)[0];
            ori[1] = test_CURRENTORIENT(boy)[1];
            ori[2] = test_CURRENTORIENT(boy)[2];
            if (_AbsRotyGV(ori, dir) < 60) {
                if (dist < 270.0) {
                    if (GetFlyPosition((float *)((char *)GOBJ_ACT(self)->work + 0x8A0), mpos,
                                       bpos) == 0) {
                        debug_StdPrintfDummy("not found");
                    }
                    inc = 1;
                    if (half < GOBJ_WORK(self)->nakaBossCount) {
                        ACTSendMailCorrect(self, 0x1D);
                    } else if (dist < 120.0) {
                        ACTSendMailCorrect(self, 0x113);
                    }
                } else {
                    ACTSendMailCorrect(self, 0x113);
                }
            } else if (dist < 200.0f) {
                ACTSendMailCorrect(self, 0x113);
            }
        }
        sub = GOBJ_ACT(self);
        if (inc != 0) {
            ((ActWork *)sub->work)->nakaBossCount = ((ActWork *)sub->work)->nakaBossCount + 1;
        } else {
            ((ActWork *)sub->work)->nakaBossCount = 0;
        }
    }
}

/* a file-scope helper expanded once inside subEnemyBrain_ToBoy */
static inline int isNearestEnemyToBoy(GObj *self, GObj *boy, float *pos) /* derived name */
{
    GObj *found = 0;
    float best = 3.40282347e+38f /* FLT_MAX */;
    GObj *g;
    float d;

    pos[0] = test_CURRENTROOT(boy)[0];
    pos[1] = test_CURRENTROOT(boy)[1];
    pos[2] = test_CURRENTROOT(boy)[2];
    for (g = isysGObjSearchFromObjKindID_begin(4); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        d = _DistSqGV(pos, test_CURRENTROOT(g));
        if (d < best) {
            best = d;
            found = g;
        }
    }
    return self == found;
}

void subEnemyBrain_ToBoy(GObj *volatile self)
{
    float v[4];
    float w[4];
    Act *sub = GOBJ_ACT(self);
    GObj *boy = boyGObj;
    int cnt = 0;
    int i, j;
    int mode;
    int r;
    unsigned char ret;

    void ChangeBrain_ToAttack(void)
    {
        if (isLiftBoyEnable() != 0) {
            if (GOBJ_ACT(self)->enemy->sizeClass == 2) {
                BrainModeTarget *tgt = &brainTarget;

                /* the default target, then the boy; the same shape sits in
                   the other four arms (here and in ChangeBrain_ToKidnap) */
                brainTarget = brainTargetNone;
                brainTarget.gobj = boyGObj;
                if ((int)(random_unit() * 10.0f) % 100 < GOBJ_ACT(self)->enemy->attackChance2) {
                    _BrainMode_SetDirect(self, 12, tgt);
                } else {
                    _BrainMode_SetDirect(self, 9, tgt);
                }
            } else {
                /* the default target first, see the kind == 2 arm above */
                brainTarget = brainTargetNone;
                brainTarget.gobj = boyGObj;
                _BrainMode_SetDirect(self, 9, &brainTarget);
            }
        }
    }

    while (1) {
        mode = 0;
        r = (int)(random_unit() * 10.0f) % 100;
        debug_StdPrintfDummy("**toboy function start :: count=[%d]\n", cnt++);
        ret = _ApproachTarget(self, boy, (char *)sub + 0x120, NakaBoss, 200.0f, 0);
        v[0] = test_CURRENTROOT((void *)self)[0];
        v[1] = test_CURRENTROOT((void *)self)[1];
        v[2] = test_CURRENTROOT((void *)self)[2];
        if (ret != 0) {
            debug_StdPrintfDummy("await start\n");
            for (i = 0; i < (60 - systemStatus[0] * 10) / systemStatus[1] * 75 / 60; i++) {
                mode = 0;
                switch (Battle_isCurrentStatus(self, boy, v)) {
                case 0:
                    break;
                case 1:
                    mode = 4;
                    if (r < GOBJ_ACT(self)->enemy->attackChance) {
                        mode = 3;
                    }
                    if (((int)(*(long long *)((char *)GOBJ_ACT(self)->enemy + 0x210) >> 1)) & 1) {
                        mode = 3;
                    }
                    if (debug_ignore_dodge != 0) {
                        mode = 4;
                    }
                    if (mode != 3) {
                        if (!(_DistSqGV(test_CURRENTROOT((void *)self),
                                        test_CURRENTROOT((void *)boyGObj)) < 22500.0f)) {
                            mode = 0;
                        }
                    }
                    break;
                case 2:
                    mode = 2;
                    break;
                case 3:
                    mode = 3;
                    if (GOBJ_ACT(self)->enemy->liftKind == 3) {
                        mode = 4;
                    }
                    if (debug_ignore_dodge != 0) {
                        mode = 4;
                    }
                    debug_StdPrintfDummy("!!! wwarning !!!\n");
                    break;
                case 4:
                    mode = 5;
                    break;
                default:
                    debug_StdPrintfDummy("return value error :: [Battle_isCurrentStatus]\n");
                    break;
                }
                if (mode != 0) {
                    goto result;
                }
                sub->stick.mag = 0.0f;
                sub->dir[0] = 0.0f;
                sub->dir[1] = 0.0f;
                sub->dir[2] = 0.0f;
                _DoAwait(self);
                NakaBoss(self, 0, 0.0f);
                _ACTWait(1);
            }
            debug_StdPrintfDummy("await end\n");
            if ((((int)(*(long long *)((char *)GOBJ_ACT(self)->enemy + 0x210) >> 1)) & 1) == 0) {
                mode = 4;
            }
        } else {
            mode = 1;
        }
    result:
        debug_StdPrintfDummy("toboy ra is [%d]\n", mode);
        if (stage_no == 86 || stage_no == 3 || stage_no == 46) {
            if (mode == 4 || mode == 5) {
                mode = 3;
            }
        }
        switch (mode - 1) {
        case 0:
            ACTSendMailCorrect(self, 0x100);
            break;
        case 1:
            break;
        case 2:
            if (IsBoyStatus_NotDanger() != 0) {
                if ((char *)girlGObj != 0) {
                    if (GOBJ_ACT(girlGObj)->actMode != 0x6F) {
                        break;
                    }
                }
            }
            ACTSendMailCorrect(self, 0x113);
            break;
        case 3:
            if (isNearestEnemyToBoy(self, boyGObj, w) && EnemyUtil_isOtherStatus(self, 0) == 0) {
                ChangeBrain_ToAttack();
            }
            break;
        case 4:
            if (EnemyUtil_isOtherStatus(self, 0) == 0) {
                ChangeBrain_ToAttack();
            }
        }
        sub->stick.mag = 0.0f;
        sub->dir[0] = 0.0f;
        sub->dir[1] = 0.0f;
        sub->dir[2] = 0.0f;
        for (j = 0; j < (60 - systemStatus[0] * 10) / systemStatus[1] * 90 / 60; j++) {
            if (mode == 5) {
                if (EnemyUtil_isOtherStatus(self, 0) == 0) {
                    ChangeBrain_ToAttack();
                }
            }
            _DoAwait(self);
            NakaBoss(self, 0, 0.0f);
            _ACTWait(1);
        }
    }
}

inline void subEnemyBrain_BodyGuard(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    GObj *tgt = sub->brainTarget;
    float *pos = (float *)((char *)sub + 0x120);

    while (1) {
        if (_DistGV(test_CURRENTROOT(self), test_CURRENTROOT(tgt)) < 200.0f) {
            _ACTWait(1);
        } else {
            if ((unsigned char)_ApproachTarget(self, tgt, pos, 0, 100.0f, 0) == 0) {
                sub->stick.mag = 0;
                *(int *)((char *)sub + 0x120) = 0;
                *(int *)((char *)sub + 0x124) = 0;
                *(int *)((char *)sub + 0x128) = 0;
                _ACTWait(30);
            }
            sub->stick.mag = 0;
            *(int *)((char *)sub + 0x120) = 0;
            *(int *)((char *)sub + 0x124) = 0;
            *(int *)((char *)sub + 0x128) = 0;
            _ACTWait(60);
        }
    }
}

void subEnemyBrain_ToGirl(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    float p0[4];
    float p1[4];
    int i;
    int found;

    void ChangeBrain_ToKidnap(void)
    {
        switch (GOBJ_ACT(self)->enemy->sizeClass) {
        case 0:
            /* the default target first, see ChangeBrain_ToAttack */
            brainTarget = brainTargetNone;
            brainTarget.gobj = girlGObj;
            _BrainMode_SetDirect(self, 8, &brainTarget);
            break;
        case 2:
            brainTarget = brainTargetNone;
            brainTarget.gobj = girlGObj;
            _BrainMode_SetDirect(self, 11, &brainTarget);
            break;
        default:
            brainTarget = brainTargetNone;
            brainTarget.gobj = girlGObj;
            _BrainMode_SetDirect(self, 10, &brainTarget);
            break;
        }
    }

    GObj *girl = girlGObj;

    sub->stick.mag = 0.0f;
    sub->dir[0] = 0.0f;
    sub->dir[1] = 0.0f;
    sub->dir[2] = 0.0f;
    for (i = 0; i < (60 - systemStatus[0] * 10) / systemStatus[1]; i++) {
        _DoAwaitGirl(self);
        _ACTWait(1);
    }
    found = (unsigned char)_ApproachTarget(self, girl, (char *)sub + 0x120, (void *)enemy_dodge,
                                           130.0f, 0);
    GetRootProjectionPosOfGObj(p0, girl);
    GetRootProjectionPosOfGObj(p1, self);
    if (50.0f < (p0[1] - p1[1] < 0.0f ? -(p0[1] - p1[1]) : p0[1] - p1[1])) {
        found = 0;
    }
    if (found == 0) {
        sub->stick.mag = 0.0f;
        sub->dir[0] = 0.0f;
        sub->dir[1] = 0.0f;
        sub->dir[2] = 0.0f;
        _ACTWait(30);
        eBrainSendMes((void *)self, 5);
        ACTSendMailCorrect(self, 0x100);
        _ACTWait(0);
    }
    while (1) {
        debug_StdPrintfDummy("change to kidnap");
        ChangeBrain_ToKidnap();
        _ACTWait(1);
    }
}

static int _ApproachTarget_Boss(GObj *self, void *tgt, void *pos, void *fn, float range,
                                unsigned char flag)
{
    float p0[4];
    float p1[4];
    Act *sub = GOBJ_ACT(self);

    for (;;) {
        GetRootProjectionPosOfGObj(p0, tgt);
        GetRootProjectionPosOfGObj(p1, self);
        if (fn != 0) {
            ((void (*)(GObj *, void *, float))fn)(
                self, tgt, _DistGV(test_CURRENTROOT(self), test_CURRENTROOT(tgt)));
        }
        sub->stick.mag = 1.0f;
        _OrientXZGV((float *)pos, p0, p1);
        if (_DistxzSqGV(p0, p1) < 160000.0f && -50.0f < -(p0[1] - p1[1]) &&
            p1[1] - p0[1] < 500.0f && enemyCheckTurnAngle(self) == 0 && sub->actMode != 10) {
            return 1;
        }
        _ACTWait(1);
    }
}

static int flyMailCore(void *self)
{
    int flyLow = 0;
    int flyHigh = 0;
    int ret = 0;
    GObj *gen;

    switch (CanThisEnemyFly(self)) {
    case 1:
        flyLow = 1;
        break;
    case 2:
        flyLow = 1;
        flyHigh = 1;
        break;
    }
    if (isEnemyActive(self) == 0) {
        goto end;
    }
    if (IsEnemyBrainToGenerator(self, &gen)) {
        if (flyHigh == 0 && debug_enemy_fly_with_girl == 0) {
            goto end;
        }
        ACTSendMailCorrect(self, 0x1E);
        ret = 1;
    } else {
        if (flyLow == 0) {
            goto end;
        }
        ACTSendMailCorrect(self, 0x1D);
        ret = 1;
    }
end:
    return ret;
}

inline int FlyMail(void *self)
{
    int x = GOBJ_ACT(self)->frame;
    if (x < 0xC) {
        return -1;
    }
    return flyMailCore(self);
}

/* a static inline, expanded twice inside _ApproachTarget_Way */
static inline unsigned char waitEnemyFly(GObj *self) /* derived name */
{
    Act *sub = GOBJ_ACT(self);

    while (sub->actMode != 6) {
        if (FlyMail(self) == 0) {
            return 0;
        }
        _ACTWait(1);
    }
    return 1;
}

/* a static inline */
static inline int flyLimitMail(GObj *self, float *rp) /* derived name */
{
    Act *sub = GOBJ_ACT(self);

    if (sub->frame < 0xC) {
        return 0;
    }
    GetRootPosition(rp, self);
    if (GetFlyLimitClearance(rp) == 0) {
        return 0;
    }
    flyMailCore(self);
    return 1;
}

static int _ApproachTarget_Way(GObj *self, void *tgt, void *pos, void *fn, float range,
                               unsigned char flag)
{
    float p0[4];
    float p1[4];
    float rp[4];
    Act *sub = GOBJ_ACT(self);
    int i;
    int ret;

    GetRootProjectionPosOfGObj(p0, tgt);
    GetRootProjectionPosOfGObj(p1, self);
    for (i = 0; i < (60 - systemStatus[0] * 10) / systemStatus[1] * 40 / 60; i++) {
        if (IsSelectID_EnemyCtrl(*(int *)((char *)self + 8)) != 0) {
            break;
        }
        _ACTWait(1);
    }
    ret = ACTWayMove_BeginDetail(self, p1, p0, tgt, 0, 0);
    if (ret == 0) {
        ret = waitEnemyFly(self);
        if (ret == 0) {
            return 0;
        }
    }
    while (1) {
        GetRootProjectionPosOfGObj(p0, tgt);
        GetRootProjectionPosOfGObj(p1, self);
        if (!(_DistSqGV(p1, p0) < 1440000.0f) ||
            140.0f < ((p1[1] - p0[1] < 0.0f) ? -(p1[1] - p0[1]) : (p1[1] - p0[1]))) {
            flyLimitMail(self, rp);
        }
        if (fn != 0) {
            ((void (*)(GObj *, void *, float))fn)(
                self, tgt, _DistGV(test_CURRENTROOT(self), test_CURRENTROOT(tgt)));
        }
        if (sub->actMode == 6) {
            _ACTWait(1);
            continue;
        }
        if (ACTWayMove_NextDetail(self, pos, p0, 0, 0) == 0) {
            if (waitEnemyFly(self) == 0) {
                return 0;
            }
        }
        *(float *)((char *)pos + 0) = sub->wayNodeX;
        *(float *)((char *)pos + 4) = sub->wayNodeY;
        *(float *)((char *)pos + 8) = sub->wayNodeZ;
        if (((int)(((ActStatusWord *)((char *)sub + 0x3F0))->q >> 17)) & 1) {
            if (debug_fly_limit_test != 0) {
                static int col[4] = {255, 100, 0, 128};

                MatrixDrive_PushMatrix();
                GetRootPosition(rp, self);
                _UnitMatrix(MatrixDrive_GetMatrix());
                MatrixDrive_TransMatrixV((char *)rp);
                gif_StartPacketPri(11);
                prim_DispWireSphere(100.0f, col, 4, 4);
                gif_EndPacket();
                MatrixDrive_PopMatrix();
            }
            FlyMail(self);
        }
        if (*(int *)((char *)self + 8) == 0xEAD && (((int)(sub->flags20.ll >> 39)) & 1)) {
            FlyMail(self);
        }
        if (stage_no == 9 && CheckFloorAttribute(self, 0x100000) != 0 &&
            (tgt == (void *)boyGObj || tgt == (void *)((char *)girlGObj)) &&
            _DistxzSqGV(test_CURRENTROOT(self), test_CURRENTROOT(tgt)) < 40000.0f &&
            ((test_CURRENTROOT(self)[1] - test_CURRENTROOT(tgt)[1] < 0.0f)
                 ? -(test_CURRENTROOT(self)[1] - test_CURRENTROOT(tgt)[1])
                 : (test_CURRENTROOT(self)[1] - test_CURRENTROOT(tgt)[1])) < 150.0f) {
            return 1;
        }
        if (tgt == (void *)((char *)girlGObj) && _DistxzSqGV(p1, p0) < 10000.0f &&
            ((p1[1] - p0[1] < 0.0f) ? -(p1[1] - p0[1]) : (p1[1] - p0[1])) < 50.0f &&
            WayMove_CheckCollis(p1, p0, 0, 0) == 0) {
            return 1;
        }
        if ((((int)(((ActStatusWord *)((char *)sub + 0x3F0))->q >> 17)) & 1) == 0 &&
            sub->wayGoalDist < range && sub->wayGoalHeight < 100.0f &&
            ((sub->wayGoalHeight < 0.0f) ? -sub->wayGoalHeight : sub->wayGoalHeight) < 200.0f) {
            return 1;
        }
        if (sub->wayGoalDist < 200.0f) {
            sub->stick.mag = 0.5f;
        } else if (ACTWay_IsMustWalkFromWay(self) != 0) {
            sub->stick.mag = 0.5f;
        } else {
            sub->stick.mag = 1.0f;
        }
        if (flag != 0) {
            SetMotionDirection(self, (float *)((char *)sub + 0x120));
            flag = 0;
        }
        _ACTWait(1);
    }
    /* disabled in retail: the motion-request timer and mail reports */
    if (0) {
        debug_StdPrintfDummy("_ACTMotReqTimer wait\n");
        debug_StdPrintfDummy("_ACTMotReqTimer error loop\n");
        debug_StdPrintfDummy("\tmail[%d] can not accept\n");
    }
}

inline int _ApproachTarget(GObj *self, void *tgt, void *pos, void *fn, float range,
                           unsigned char flag)
{
    if (GOBJ_ACT(self)->enemy->liftKind != 3) {
        return _ApproachTarget_Way(self, tgt, pos, fn, range, flag);
    } else {
        return _ApproachTarget_Boss(self, tgt, pos, fn, range, flag);
    }
}

inline int isEnemyKidnapEnable(GObj *self)
{
    if (GOBJ_ACT(self)->enemy->liftKind == 0) {
        return 0;
    }
    return actEnemyFlagCheckActive(self);
}

inline int GetEnemyType(float x, float y, float z)
{
    return 1;
}

inline int GetEnemyTypeFromGObj(GObj *obj)
{
    return GOBJ_ACT(obj)->enemy->liftKind;
}

inline int GetMotherGeneratorLabelAskEnemy(GObj *enemy)
{
    return GOBJ_WORK(enemy)->motherLabel;
}

inline GObj *GetMotherGeneratorGObjAskEnemy(GObj *enemy)
{
    return GOBJ_WORK(enemy)->motherGObj;
}

/* the bit-51 store to the actor word is a union access, the four 0.05f
 * stores go through a union view of the gobj+0x15C slot, and each life pair
 * is one chained assignment */
void actEnemyStart(GObj *self)
{
    char *act;
    int alive;
    float life;

    debug_StdPrintfDummy("actEnemyStart:%p\n", self);
    act = actInitialize(self);
    actInitialize_ext_charcter(self);
    actInitialize_only_charcter(self);
    actInitialize_geo(self);
    if (*(int *)((char *)self + 8) == 3757) {
        *(long long *)(act + 0x20) = *(long long *)(act + 0x20) | 0x40000000;
    }
    ACTGame_LwsEffectInit(self);
    ACTParaStatus_Init(self);
    _ACTCharStatus_Init((int **)self);
    GOBJ_WORK(self)->bga = InitMultiBgaManager(1);
    {
        EnemyStartRec p[4] = {
            {0, 35, 18, 90, 0, _ACTGame_GetParamF(20), 369.0f, 1},
            {1, 0, 18, 50, 0, _ACTGame_GetParamF(21), 369.0f, 0},
            {2, 34, 22, 25, 50, _ACTGame_GetParamF(22), 369.0f, 0},
            {2, 33, 22, 0, 100, 3.40282347e+38f /* FLT_MAX */, 370.0f, 1},
        };
        unsigned long long bit;

        GOBJ_ACT(self)->enemy->bodySize = *(float *)((int)GOBJ_SUB(self)->nodes + 0x20);
        GOBJ_ACT(self)->enemy->liftKind = 1;
        GOBJ_ACT(self)->enemy->sizeClass = p[1].sizeClass;
        GOBJ_ACT(self)->enemy->paraStatus = p[1].paraStatus;
        GOBJ_ACT(self)->enemy->clingNode = p[1].clingNode;
        GOBJ_ACT(self)->enemy->attackChance = p[1].attackChance;
        GOBJ_ACT(self)->enemy->attackChance2 = p[1].attackChance2;
        *(float *)(act + 0x1E4) = p[1].maxLife;
        GOBJ_ACT(self)->enemy->bodyslamMail = (int)p[1].bodyslamMail;
        GOBJ_ACT(self)->enemy->bossLife = 3;
        bit = p[1].noGuard;
        ((ActStatusWord *)(act + 0x18))->q =
            (((ActStatusWord *)(act + 0x18))->q & ~(1ULL << 51)) | ((bit & 1) << 51);
    }
    if (GOBJ_ACT(self)->enemy->liftKind == 3) {
        *(float *)(((EnemySubSlot *)((char *)self + 0x15C))->p + 0x45C) = 0.05f;
        *(float *)(((EnemySubSlot *)((char *)self + 0x15C))->p + 0x460) = 0.05f;
        *(float *)(((EnemySubSlot *)((char *)self + 0x15C))->p + 0x464) = 0.05f;
        *(float *)(((EnemySubSlot *)((char *)self + 0x15C))->p + 0x468) = 0.05f;
    }
    GOBJ_ACT(self)->enemy->battleType = debug_enemy_battle_type;
    setBattleStatus(self);
    alive = 0;
    if (actEnemyFlagCheckDead(self) != 0) {
        alive = 1;
    }
    if (alive != 0) {
        *(long long *)(act + 0x18) = *(long long *)(act + 0x18) & ~(1LL << 32);
        *(long long *)(act + 0x18) = *(long long *)(act + 0x18) & ~(1LL << 33);
    }
    _ACTWait(1);
    GOBJ_WORK(self)->motherLabel = GetMotherGenerator(*(int *)((char *)self + 8));
    if (GOBJ_WORK(self)->motherLabel != -1) {
        GOBJ_WORK(self)->motherGObj = isysGObjSearchFromObjLayoutID(GOBJ_WORK(self)->motherLabel);
    }
    *(IntrMail **)(act + 0xD0) = &actIntrList[74];
    if (debug_brain_flag != 0) {
        actCreateSubThread((void *)subEnemyBrainMain, 20);
    }
    actCreateSubThread((void *)subEnemyControl, 21);
    actCreateSubThread((void *)subEnemyCollision, 21);
    actCreateSubThread((void *)subCommonIdle, 21);
    *(IntrMail **)(act + 0xD4) = &actIntrList[79];
    *(ActKind *)(act + 0x48) = ACT_KIND_ENEMY;
    life = getEnemyRestartLife(self);
    *(float *)(act + 0x1E0) = *(float *)(act + 0x1E4) = life;
    if (life < 10.0f) {
        *(float *)(act + 0x1E0) = *(float *)(act + 0x1E4) = 10.0f;
    }
    *(int *)(act + 0x350) = 0;
    ACTSendMailCorrect(self, 199);
    if (alive != 0) {
        actEnemyHyde(self);
    }
    _ACTWait(0);
}

inline void subEnemyBrain_Irregular(GObj *volatile self)
{
    EnemyBrainWork *sub = (EnemyBrainWork *)self->act;

    sub->flags &= ~(1LL << 34);
    eBrainSendMes(self, 4);
    if (isEnemyCarriedByGirl(self)) {
        afterCommonCarry(self);
    }
    while (1) {
        _ACTWait(30);
        _BrainMode_SetDirect(self, 0, 0);
    }
}

void subEnemyBrain_Attack(GObj *volatile self)
{
    int i;

    for (i = 0; i < (60 - systemStatus[0] * 10) / systemStatus[1] / 6; i++) {
        enemyDodgeSendMail(self);
        _DoAwait(self);
        if (_MustChase(self) != 0) {
            break;
        }
        _ACTWait(1);
    }
    for (i = 0; i < (60 - systemStatus[0] * 10) / systemStatus[1] * 100 / 60; i++) {
        if (i < (60 - systemStatus[0] * 10) / systemStatus[1] * 50 / 60) {
            enemyDodgeSendMail(self);
        }
        _DoAwait(self);
        if ((60 - systemStatus[0] * 10) / systemStatus[1] * 80 / 60 < i) {
            if (_MustChase(self) != 0) {
                break;
            }
        }
        _ACTWait(1);
    }
    _BrainMode_SetDirect(self, 0, 0);
    _ACTWait(0);
}

void subEnemyBrain_Cling(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    int tgt = GOBJ_ACT(self)->enemy->target;
    float v[4];

    GOBJ_ACT(self)->enemy->clingReq = tgt;
    _OrientXZGV(v, test_CURRENTROOT(tgt), test_CURRENTROOT(self));
    sub->dir[0] = v[0];
    sub->dir[1] = v[1];
    sub->dir[2] = v[2];
    SetMotionDirection((void *)self, v);
    ACTSendMailCorrect((void *)self, 0xC2);
    _ACTWait(1);
    _ACTWait(1);
    while (1) {
        if (sub->actMode == 4 || sub->actMode == 0x10) {
            if (_DistSqGV(test_CURRENTROOT(tgt), test_CURRENTROOT(self)) < 3600.0f) {
                ACTSendMailCorrect((void *)self, 0xD0);
            }
        } else {
            _ACTWait(30);
            _BrainMode_SetDirect(self, 0, 0);
        }
        _ACTWait(1);
    }
}

inline void subEnemyBrain_Shoulder(GObj *volatile self)
{
    float *dir = (float *)((char *)GOBJ_ACT(self) + 0x120);
    float *girl = test_CURRENTROOT((girlGObj));
    float *me = test_CURRENTROOT(self);
    _OrientXZGV(dir, girl, me);
    SetMotionDirection((void *)self, dir);
    ACTSendMailCorrect((void *)self, 0x162);
    while (1) {
        _ACTWait(120);
        _BrainMode_SetDirect(self, 0, 0);
    }
}

inline void subEnemyBrain_Pickup(GObj *volatile self)
{
    ACTSendMailCorrect((void *)self, 0x16C);
    while (1) {
        _ACTWait(120);
        _BrainMode_SetDirect(self, 0, 0);
    }
}

inline void subEnemyBrain_Bodyslam(GObj *volatile self)
{
    if (GOBJ_ACT(self)->enemy->liftKind == 3) {
        ACTSendMailCorrect((void *)self, 0x175);
    } else {
        ACTSendMailCorrect((void *)self, 0x173);
    }
    while (1) {
        _ACTWait(120);
        _BrainMode_SetDirect(self, 0, 0);
    }
}
