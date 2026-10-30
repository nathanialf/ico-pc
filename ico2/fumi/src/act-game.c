#include "ee_view.h"
#include "typedef.h"
#include "act-game.h"
#include "debug.h"
#include "gamesys.h"
#include "main.h"
#include "sceneManager.h"
#include "act-env.h"
#include "act-wish.h"
#include "girl_act.h"
#include "stage_orient.h"
#include "mail-add-data.h"
#include "script.h"
#include "StageAnimation.h"
#include "clipCollisionManager.h"
#include "matrixDrive.h"
#include "motionManager.h"
#include "motionOrientManager.h"
#include "camera-editor.h"
#include "camera-root.h"
#include "multiBgaManager.h"
#include "quaternion.h"
#include <stdlib.h>
#include "motionManager2.h"
#include "geometryManager.h"
#include "act-parallel-control.h"
#include "brain.h"
#include <string.h>
#include "gflag.h"
#include "Matrix.h"
#include "debug_exception.h"
#include <libvu0.h>
#include "enemy_act.h"
#include "gobj.h"
#include "act_bird.h"
#include "item.h"
#include "commonact.h"
#include "obj_manager.h"
#include "gv.h"
#include "fieldCollision.h"
#include <assert.h>
#include "poly-flat.h"
#include "act.h"
#include "brain.h"

/* weapon.h's WeaponDef, which this TU does not include (CheckWeaponKind
   differs): the weapon kind's motion orient at 0x1C */
typedef struct { /* field names derived */
    char pad0[28];
    int motOrient;
    char pad20[4];
} WeaponEntry; /* derived name */

typedef struct { /* field names derived */
    long long w;
} __attribute__((packed)) U64ag; /* derived name */

/* The 0x194-byte-per-entry (JPN and KOR 0x190) motion record table, indexed by the object's
   current motion id (obj->0x15C->0x4A0); the fields this TU reads, named as
   motionOrientManager.h's MotionDef. */
typedef struct { /* field names derived */
    char pad0[336];
    int playMode;
    char pad154[44];
    short priInputBegin;
    short girlDirFrames;
    short priInputEnd;
    char pad186[2];

    union {
        unsigned int w;

        struct {
            unsigned short lo, hi;
        } h;

        char b;
    } modeBits;

    unsigned int flags;
    unsigned int flags2;
} MotionRec; /* derived name */

extern MotionRec motionKind[];

typedef struct { /* field names derived */
    float x, y, z, w;
} __attribute__((aligned(16))) Vec4S; /* derived name */

/* as in boyact.h, which this TU does not include (PrivInsCamSet differs) */
extern void SetBoyInfo(GObj *weapon, GObj *item);
/* as in boyact.h, which this TU does not include (PrivInsCamSet differs) */
extern void BoyInfoUpdate_StageChange(void);

/* One table: a 100-entry object list, two parallel per-entry int arrays
   (the full view result and the simple one), the entry count and the
   round-robin cursor the loop below advances one entry per frame. */
typedef struct {     /* field names derived */
    GObj *obj[100];  /* 0x000 */
    int view[100];   /* 0x190 */
    int simple[100]; /* 0x320 */
    int num;         /* 0x4B0 */
    int cur;         /* 0x4B4 */
} ActGameViewTbl;    /* derived name */

/* the view table described below, the tail of act-game.o's .bss; no code
   reaches the 0x440 bytes ahead of it */
static ActGameViewTbl actGameView; /* derived name */

/* One 0x50-byte record per act status, indexed by sub->0x34. */

/* the motion-play-speed-ratio mode at work+0x54, which
   ACTGame_SetMotionPlaySpeedRatio_Exec dispatches on */
typedef enum { MPSR_OFF, MPSR_ONESHOT, MPSR_HOLD } MpsrMode;

/* self->0x164->0x688, the per-actor motion work block, re-derived at every
   use */
#define ACTWORK(g) ((char *)GOBJ_ACT(g)->work) /* derived name */

/* weapon.h declares CheckWeaponKind(char *) */
extern int CheckWeaponKind(GObj *self);

/* The actor's orient-request bitfield: three 64-bit request words at
   sub+0x478, each paired with the permission mask 16 bytes further on. */
/* s is the Act (as a char *); its wish words are not at 0x478 here */
#define ORQ(s, i) (((ActStatusWord *)&((Act *)(s))->wish0)[i].q)
#define ORM(s, i) (((ActStatusWord *)&((Act *)(s))->wish0)[(i) + 2].q)
#define ORBIT(w, b) ((int)((w) >> (b)) & 1) /* derived name */

/* handClInfoClear is the cleared template each frame's hand-link probe record
   (ActWork.handCl, act-game.h) starts from. */

static HandClInfo handClInfoClear = {0}; /* derived name */

/* The hand-mode rows the motion record's two hand nibbles index: 16 bytes a
   row, the mode RequestChangeHandMode wants in the last word. */
typedef struct { /* field names derived */
    char pad0[12];
    int mode;
} HandModeRow; /* derived name */

extern HandModeRow motionIKEffKind[];
static void ACTItemWatchMotion(GObj *self);
extern int ReleaseWeaponWithFumbleSequential(GObj *g);
/* as in boyact.h, which this TU does not include */
extern void PrivInsCamSet(float *pos, float *tgt, GObj *track, int inFrames, int outFrames,
                          float inRate, float blend, unsigned char control);
/* as in weapon.h, which this TU does not include (CheckWeaponKind differs) */
extern ICO_WORD_PTR(GObj *) GetTorchGObjOfWeapon(GObj *weapon);
extern WeaponEntry weaponKind[];

inline void ACTGameCollisionOff(volatile int *self)
{
    GOBJ_SUB(self)->ctrl.floorFit = 0;
    GOBJ_SUB(self)->ctrl.cliffWallCheck = 0;
    GOBJ_SUB(self)->ctrl.wallReact = 0;
    ICO_RAW(int, (int *)self[0x57], 0x7C, GOBJ_SUB(self)->cylinderOn) = 0;
}

inline void ACTGameCollisionOn(volatile int *self)
{
    GOBJ_SUB(self)->ctrl.floorFit = 1;
    GOBJ_SUB(self)->ctrl.cliffWallCheck = 1;
    GOBJ_SUB(self)->ctrl.wallReact = 1;
    ICO_RAW(int, (int *)self[0x57], 0x7C, GOBJ_SUB(self)->cylinderOn) = 1;
}

inline int ACTGame_CheckHandMotion(GObj *boy, GObj *girl)
{
    MotionRec *rec0 = &motionKind[GOBJ_SUB(boy)->ctrl.motion];
    MotionRec *rec1 = &motionKind[GOBJ_SUB(girl)->ctrl.motion];
    int b0 = (rec0->flags >> 18) & 1;
    int b1 = (rec1->flags >> 18) & 1;
    return b0 & b1;
}

static inline int ACTGame_CheckItemMotion_ntscaug6jun26(GObj *self)
{
    const MotionRec *rec = &motionKind[GOBJ_SUB(self)->ctrl.motion];
    /* PC port: the host holds the item mode at the European bits 19-21 */
    return (rec->modeBits.w >> 19) & 7;
}

inline int ACTGame_CheckItemMotion(GObj *self)
{
    if (ICO_NTSCAUG6JUN26) {
        return ACTGame_CheckItemMotion_ntscaug6jun26(self);
    }
    MotionRec *rec = &motionKind[GOBJ_SUB(self)->ctrl.motion];
    return (rec->modeBits.w >> 19) & 7;
}

static void ACTGame_SaveActorInformation_jun26(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    if (((int)((s->flags18.ll >> 34)) & 1) && s->modeFrame % 30 == 0) {
        gamesysObjInfoPosSetStage(self, s->infoPos, 0, stage_no);
    }
}

void ACTGame_SaveActorInformation(GObj *self)
{
    if (ICO_JUN26) {
        ACTGame_SaveActorInformation_jun26(self);
        return;
    }
    Act *s = GOBJ_ACT(self);
    if (((int)(PAL_NTSCAUG6JUN26(s->flags18.ll >> 39, s->flags18.ll >> 35)) & 1) &&
        s->modeFrame % 30 == 0) {
        gamesysObjInfoPosSetStage(self, s->infoPos, 0, stage_no);
    }
}

void ACTGame_DeleteActorInformation(GObj *self)
{
    gamesysObjInfoCls(self->kind, self->labelId);
}

static void EXITDATA_GetNextPosition(int idx, float *pos, float *rot)
{
    exit_no = idx;
    test_nextstage_firstwalk_set(idx, exitData[idx].firstWalk0, exitData[idx].firstWalk1,
                                 exitData[idx].firstWalk2);

    pos[0] = -exitData[idx].pos[0];
    pos[1] = -exitData[idx].pos[1];
    pos[2] = -exitData[idx].pos[2];

    rot[0] = exitData[idx].rot[0];
    rot[1] = exitData[idx].rot[1];
    rot[2] = exitData[idx].rot[2];

    sceVu0ScaleVector(rot, rot, 0.017453292f);
}

inline void ACTGame_StageChangeGObjID(int no, int kind, int idx)
{
    float tmp_a[4];
    float tmp_b[4];
    EXITDATA_GetNextPosition(idx, tmp_a, tmp_b);
    gamesysObjInfoPosNewStageSet(no, kind, exitData[idx].nextStage, tmp_a, tmp_b);
}

void ACTGame_StageChangeGObj(GObj *self, int idx)
{
    float tmp_a[4];
    float tmp_b[4];
    float buf[4];
    Vec4S buf2;
    Vec4S buf3;

    EXITDATA_GetNextPosition(idx, tmp_a, tmp_b);
    if (self->kind == 0x11) {
        memset(buf, 0, 0x10);
        buf[2] = 250.0f;
        _ApplyRyGV(buf, -tmp_b[1]);
        sceVu0AddVector(tmp_a, tmp_a, buf);
    }
    if (self == girlGObj) {
        if (0.0f <= GOBJ_WORK(self)->escortOffset) {
            memset(&buf3, 0, 0x10);
            buf3.z = -GOBJ_WORK(girlGObj)->escortOffset;
            buf2 = buf3;
            _ApplyRyGV(&buf2.x, -tmp_b[1]);
            sceVu0AddVector(tmp_a, tmp_a, &buf2);
        }
    }
    gamesysObjInfoPosNewStageSet(self->labelId, self->kind, exitData[idx].nextStage, tmp_a, tmp_b);
}

inline void ACTGame_StageChangeGObjDirect(GObj *self, int stage, void *dir, int deg)
{
    float buf0[4];
    float buf1[4];
    memset(buf1, 0, 0x10);
    buf1[1] = (float)deg * 3.1415927f / 180.0f;
    sceVu0ScaleVector(buf0, dir, -1.0f);
    gamesysObjInfoPosNewStageSet(self->labelId, self->kind, stage, buf0, buf1);
}

/* the first exit whose next stage is this one */
static inline int getExitIndexOfStage(int stage) /* derived name */
{
    int i;

    for (i = 0; i < PAL_NTSCAUG6_JUN26(261, 257, 256); i++) {
        if (exitData[i].nextStage == stage) {
            return i;
        }
    }
    return 0;
}

void ACTGame_SetActors_Debug(int stage, unsigned char flag)
{
    GObj *boy;
    GObj *girl;
    int idx;

    boy = 0;
    girl = 0;
    idx = getExitIndexOfStage(stage);
    gflagOn(PAL_NTSC_AUG6_JUN26(394, 361, 360, 343));
    if (flag != 0) {
        if (boyGObj != 0) {
            Act *s = GOBJ_ACT(boyGObj);
            boy = s->weapon;
            girl = s->curItem;
            SetBoyInfo(boy, girl);
            BoyInfoUpdate_StageChange();
        }
    }
    {
        int hasBoy = stageData[stage].attrTop;
        int hasGirl = stageData[stage].flag0;
        int tbl[4][3] = {
            {PAL_NTSC_AUG6_JUN26(54, 51, 50, 47), 1, hasBoy},
            {PAL_NTSC_AUG6_JUN26(148, 131, 130, 124), 2, hasGirl},
            {PAL_NTSC_AUG6_JUN26(55, 52, 51, 48), 14, hasBoy},
            {-1, -1},
        };
        float pos[4];
        float rot[4];
        int n;
        int cur;
        int obj;
        int chk;

        EXITDATA_GetNextPosition(idx, pos, rot);
        for (n = 0; tbl[n][0] != -1; n++) {
            cur = tbl[n][0];
            obj = tbl[n][1];
            chk = tbl[n][2];
            if (n == 2 && boy != 0) {
                break;
            }
            if (chk != 0) {
                if (!(cur < stageData[stage].labelTop) && cur < stageData[stage].labelEnd &&
                    gamesysObjInfoGet(obj, cur) == 0) {
                    debug_StdPrintfDummy("first\n");
                } else {
                    debug_StdPrintfDummy("set\n");
                    gamesysObjInfoPosNewStageSet(cur, obj, stage, pos, rot);
                }
                if (cur == PAL_NTSC_AUG6_JUN26(54, 51, 50, 47)) {
                    if (boy != 0) {
                        gamesysObjInfoPosNewStageSet(boy->labelId, boy->kind, stage, pos, rot);
                    }
                    if (girl != 0) {
                        gamesysObjInfoPosNewStageSet(girl->labelId, girl->kind, stage, pos, rot);
                    }
                }
            }
        }
    }
}

inline int ACTGame_FLAG_LIFEPINCH(GObj *self)
{
    if (GOBJ_ACT(self)->life <= 20.0f)
        return 1;
    return 0;
}

static inline unsigned char ACTGame_FLAG_TETSUNAGI_jun26(void)
{
    GObj *g = girlGObj;
    int flag;
    if (g == 0)
        return 0;
    flag = (int)((GOBJ_ACT(g)->flags18.ll >> 35)) & 1;
    return flag;
}

/* Returns a char-width boolean, through an `int` intermediate. */
inline unsigned char ACTGame_FLAG_TETSUNAGI(void)
{
    if (ICO_JUN26) {
        return ACTGame_FLAG_TETSUNAGI_jun26();
    }
    GObj *g = girlGObj;
    int flag;
    if (g == 0)
        return 0;
    flag =
        (int)(PAL_NTSCAUG6JUN26(GOBJ_ACT(g)->flags18.ll >> 40, GOBJ_ACT(g)->flags18.ll >> 36)) & 1;
    return flag;
}

inline int ACTGame_FLAG_TETSUNAGI_VISUAL(void)
{
    GObj *g = girlGObj;
    if (g == 0)
        return 0;
    return (int)(GOBJ_ACT(g)->flags18.ll >> 42) & 1;
}

void ACTGame_TryConnectHand(void)
{
    RequestChangeHandMode(boyGObj, 1, PAL_NTSCAUG6JUN26(5, 4), 5, girlGObj, 0, 0);
}

void ACTGame_TryDisconnectHand(void)
{
    RequestChangeHandMode(boyGObj, 1, PAL_NTSCAUG6JUN26(5, 4), 0, 0, 0, 0);
}

inline void ACTGame_ConnectHand(void)
{
    Act *s = GOBJ_ACT(((char *)girlGObj));
    RequestChangeHandMode(girlGObj, 0, PAL_NTSCAUG6JUN26(5, 4), 6, boyGObj, 0, 0);
    RequestChangeHandMode(boyGObj, 1, PAL_NTSCAUG6JUN26(5, 4), 5, girlGObj, 0, 0);
    s->flags18.ll |= (PAL_NTSCAUG6_JUN26(1ULL << 40, 1ULL << 36, 1ULL << 35));
}

void ACTGame_DisconnectHand_WithMail(void)
{
    ACTGame_DisconnectHand();
    debug_StdPrintfDummy("with mail\n");
}

inline void ACTGame_DisconnectHand(void)
{
    Act *s = GOBJ_ACT(((char *)girlGObj));
    RequestChangeHandMode(girlGObj, 0, PAL_NTSCAUG6JUN26(5, 4), 0, 0, 0, 0);
    RequestChangeHandMode(boyGObj, 1, PAL_NTSCAUG6JUN26(5, 4), 0, 0, 0, 0);
    s->flags18.ll &= ~(PAL_NTSCAUG6_JUN26(1ULL << 40, 1ULL << 36, 1ULL << 35));
}

inline unsigned char ACTGame_CheckPriInputFrame(GObj *self)
{
    short e;
    short s;

    e = motionKind[GOBJ_SUB(self)->ctrl.motion].priInputEnd;
    if ((float)e < GOBJ_SUB(self)->ctrl.animFrame && e != -1) {
        return 1;
    }
    s = motionKind[GOBJ_SUB(self)->ctrl.motion].priInputBegin;
    if (s != -1 && GOBJ_SUB(self)->ctrl.animFrame < (float)s) {
        return 1;
    }
    return 0;
}

static inline int ACTGame_GetCurrentCallStatus_jun26(GObj *self)
{
    Act *s = GOBJ_ACT(self);

    if (s->curItem != 0) {
        return 0;
    }
    switch ((int)((&motionKind[GOBJ_SUB(self)->ctrl.motion])->modeBits.w >> 16) & 3) {
    case 1:
        return 1;
    case 2:
        return 2;
    }
    if (((int)(s->wish0.ll >> 44) & 1) && ((int)(s->wish2.ll >> 12) & 1)) {
        /* a switch on the status as an unsigned index */
        switch ((unsigned int)s->actMode) {
        case 1:
        case 2:
        case 3:
            return 2;
        }
    }
    return 0;
}

static inline int ACTGame_GetCurrentCallStatus_ntscaug6(GObj *self)
{
    Act *s = GOBJ_ACT(self);

    switch ((int)((&motionKind[GOBJ_SUB(self)->ctrl.motion])->modeBits.w >> 16) & 7) {
    case 1:
        return 1;
    case 2:
        return 2;
    case 3:
        return 0;
    }
    if (((int)(s->wish0.ll >> 44) & 1) && ((int)(s->wish2.ll >> 44) & 1)) {
        /* a switch on the status as an unsigned index */
        switch ((unsigned int)s->actMode) {
        case 1:
        case 2:
        case 3:
            return 2;
        }
    }
    return 0;
}

inline int ACTGame_GetCurrentCallStatus(GObj *self)
{
    if (ICO_JUN26) {
        return ACTGame_GetCurrentCallStatus_jun26(self);
    }
    if (ICO_NTSCAUG6) {
        return ACTGame_GetCurrentCallStatus_ntscaug6(self);
    }
    Act *s = GOBJ_ACT(self);

    if (self != boyGObj) {
        return 0;
    }
    switch (motionKind[GOBJ_SUB(self)->ctrl.motion].modeBits.h.hi & 7) {
    case 1:
        return 1;
    case 2:
        return 2;
    case 3:
        return 0;
    }
    if (((int)(s->wish0.ll >> 46) & 1) && ((int)(s->wish2.ll >> 46) & 1)) {
        /* a switch on the status as an unsigned index */
        switch ((unsigned int)s->actMode) {
        case 1:
        case 2:
        case 3:
            return 2;
        }
    }
    return 0;
}

/* The boy's Act + 0x510 and + 0x520 are env.ditchPos and env.ditchDir;
   + 0x500 and + 0x4C0 (PAIR_GetPosition_BOY) are env.cliffStepPos and
   env.cliffOrient. On the host env sits further in (8-byte pointers before
   it), so the EE offsets read the boy's way-walk fields, and the girl's
   approach to a pull-up or a ditch (girl_act.c actGirlPulledReady,
   actGirlDitch3mReady) walked to a point made of them. */
inline void PAIR_GetPosition_BOY_DITCH(float *pos, float *dir)
{
    ActEnv *e = &GOBJ_ACT(boyGObj)->env;
    pos[0] = e->ditchPos[0];
    pos[1] = e->ditchPos[1];
    pos[2] = e->ditchPos[2];
    dir[0] = e->ditchDir[0];
    dir[1] = e->ditchDir[1];
    dir[2] = e->ditchDir[2];
}

static inline int PAIR_IsStatus_BOY_DITCH_jun26(void)
{
    char *b = (char *)boyGObj;

    switch ((unsigned int)GOBJ_ACT(b)->actMode) {
    case 84:
        if (motionKind[GOBJ_SUB(b)->ctrl.motion].playMode != 1) {
            break;
        }
        /* fall through */
    case 85:
    case 88:
        return 1;

    case 1:
    case 2:
    case 3:
        if (ACTGame_FLAG_TETSUNAGI()) {
            return 1;
        }
        break;
    }
    return 0;
}

static inline int PAIR_IsStatus_BOY_DITCH_ntscaug6(void)
{
    char *b = (char *)boyGObj;

    switch ((unsigned int)GOBJ_ACT(b)->actMode) {
    case 85:
        if (motionKind[GOBJ_SUB(b)->ctrl.motion].playMode != 1) {
            break;
        }
        /* fall through */
    case 86:
    case 89:
        return 1;

    case 1:
    case 2:
    case 3:
        if (ACTGame_FLAG_TETSUNAGI()) {
            return 1;
        }
        break;
    }
    return 0;
}

inline int PAIR_IsStatus_BOY_DITCH(void)
{
    if (ICO_JUN26) {
        return PAIR_IsStatus_BOY_DITCH_jun26();
    }
    if (ICO_NTSCAUG6) {
        return PAIR_IsStatus_BOY_DITCH_ntscaug6();
    }
    char *b = (char *)boyGObj;

    switch ((unsigned int)GOBJ_ACT(b)->actMode) {
    case 0x58:
        if (motionKind[GOBJ_SUB(b)->ctrl.motion].playMode != 1) {
            break;
        }
        /* fall through */
    case 0x59:
    case 0x5C:
        return 1;

    case 1:
    case 2:
    case 3:
        if (ACTGame_FLAG_TETSUNAGI()) {
            return 1;
        }
        break;
    }
    return 0;
}

inline void PAIR_GetPosition_BOY(float *pos, float *dir)
{
    ActEnv *e = &GOBJ_ACT(boyGObj)->env;
    pos[0] = e->cliffStepPos[0];
    pos[1] = e->cliffStepPos[1];
    pos[2] = e->cliffStepPos[2];
    dir[0] = e->cliffOrient[0];
    dir[1] = e->cliffOrient[1];
    dir[2] = e->cliffOrient[2];
}

static inline int PAIR_IsStatus_BOY_PULL_jun26(void)
{
    switch ((unsigned int)GOBJ_ACT(boyGObj)->actMode) {
    case 74:
    case 75:
        return 1;

    case 1:
    case 2:
    case 3:
        if (ACTGame_FLAG_TETSUNAGI()) {
            return 1;
        }
        break;
    }
    return 0;
}

static inline int PAIR_IsStatus_BOY_PULL_ntscaug6(void)
{
    switch ((unsigned int)GOBJ_ACT(boyGObj)->actMode) {
    case 75:
    case 76:
        return 1;

    case 1:
    case 2:
    case 3:
        if (ACTGame_FLAG_TETSUNAGI()) {
            return 1;
        }
        break;
    }
    return 0;
}

inline int PAIR_IsStatus_BOY_PULL(void)
{
    if (ICO_JUN26) {
        return PAIR_IsStatus_BOY_PULL_jun26();
    }
    if (ICO_NTSCAUG6) {
        return PAIR_IsStatus_BOY_PULL_ntscaug6();
    }
    switch ((unsigned int)GOBJ_ACT(boyGObj)->actMode) {
    case 0x4E:
    case 0x4F:
        return 1;

    case 1:
    case 2:
    case 3:
        if (ACTGame_FLAG_TETSUNAGI()) {
            return 1;
        }
        break;
    }
    return 0;
}

static inline int PAIR_IsStatus_GIRL_PULL_jun26(void)
{
    switch ((unsigned int)GOBJ_ACT(((char *)girlGObj))->actMode) {
    case 68:
    case 76:
    case 77:
        return 1;

    case 1:
    case 2:
    case 3:
        if (ACTGame_FLAG_TETSUNAGI()) {
            return 1;
        }
        break;
    }
    return 0;
}

static inline int PAIR_IsStatus_GIRL_PULL_ntscaug6(void)
{
    switch ((unsigned int)GOBJ_ACT(((char *)girlGObj))->actMode) {
    case 69:
    case 77:
    case 78:
        return 1;

    case 1:
    case 2:
    case 3:
        if (ACTGame_FLAG_TETSUNAGI()) {
            return 1;
        }
        break;
    }
    return 0;
}

inline int PAIR_IsStatus_GIRL_PULL(void)
{
    if (ICO_JUN26) {
        return PAIR_IsStatus_GIRL_PULL_jun26();
    }
    if (ICO_NTSCAUG6) {
        return PAIR_IsStatus_GIRL_PULL_ntscaug6();
    }
    switch ((unsigned int)GOBJ_ACT(((char *)girlGObj))->actMode) {
    case 4:
    case 0x45:
    case 0x50:
    case 0x51:
        return 1;

    case 1:
    case 2:
    case 3:
        if (ACTGame_FLAG_TETSUNAGI()) {
            return 1;
        }
        break;
    }
    return 0;
}

static inline int PAIR_IsStatus_BOY_WAIT_jun26(void)
{
    char *b = (char *)boyGObj;

    if (b != 0) {
        switch (((unsigned int)GOBJ_ACT(b)->actMode) + 4) { /* the demo's ids 4 lower */
        case 0x4E:
        case 0x58:
            if (motionKind[GOBJ_SUB(b)->ctrl.motion].playMode == 1) {
                return 1;
            }
            break;
        }
    }
    return 0;
}

inline int PAIR_IsStatus_BOY_WAIT(void)
{
    if (ICO_JUN26) {
        return PAIR_IsStatus_BOY_WAIT_jun26();
    }
    char *b = (char *)boyGObj;

    if (b != 0) {
        switch (((unsigned int)GOBJ_ACT(b)->actMode) +
                PAL_NTSCAUG6JUN26(0, 3)) { /* NTSCAUG6 ids 3 lower */
        case 0x4E:
        case 0x58:
            if (motionKind[GOBJ_SUB(b)->ctrl.motion].playMode == 1) {
                return 1;
            }
            break;
        }
    }
    return 0;
}

inline int ACTCheckCollis_WF(float f, void *p0, void *p1, void *actor, void *posout)
{
    ClipWork work;
    int flag;
    int rv;

    memset(&work, 0, sizeof(work));
    rv = 1;
    flag = actor ? GOBJ_SUB(actor)->disp : 0;
    work.radius = f;
    sceVu0CopyVector(work.pt[0], p0);
    sceVu0CopyVector(work.pt[1], p1);
    if (flag != 0) {
        GOBJ_SUB(actor)->disp = 0;
    }
    ClipWall(&work);
    if (work.wall.elem == 0) {
        ClipFloor(&work);
        if (work.floor.elem == 0) {
            rv = 0;
        }
    }
    if (posout != 0) {
        *(float *)((char *)posout + 0) = work.pt[2][0];
        *(float *)((char *)posout + 4) = work.pt[2][1];
        *(float *)((char *)posout + 8) = work.pt[2][2];
    }
    if (flag != 0) {
        GOBJ_SUB(actor)->disp = 1;
    }
    return rv & 0xFF;
}

static inline int ACTCheckCollis_W_jun26(float f, void *hand0, void *hand1, void *actor,
                                         void *posout, int *flagout)
{
    ClipWork work;
    int flag;
    int rv;

    memset(&work, 0, sizeof(work));
    rv = 1;
    flag = actor ? GOBJ_SUB(actor)->disp : 0;
    work.radius = f;
    sceVu0CopyVector(work.pt[0], hand0);
    sceVu0CopyVector(work.pt[1], hand1);
    if (flag != 0) {
        GOBJ_SUB(actor)->disp = 0;
    }
    ClipWall(&work);
    if (flagout != 0) {
        *flagout = work.attr;
    }
    if (work.wall.elem == 0) {
        rv = 0;
    }
    if (posout != 0) {
        *(float *)((char *)posout + 0) = work.pt[2][0];
        *(float *)((char *)posout + 4) = work.pt[2][1];
        *(float *)((char *)posout + 8) = work.pt[2][2];
    }
    if (flag != 0) {
        GOBJ_SUB(actor)->disp = 1;
    }
    return rv & 0xFF;
}

inline int ACTCheckCollis_W(float f, void *hand0, void *hand1, void *actor, void *posout,
                            void *magtarget, int *flagout)
{
    if (ICO_JUN26) {
        return ACTCheckCollis_W_jun26(f, hand0, hand1, actor, posout, flagout);
    }
    ClipWork work;
    int flag;
    int rv;
    struct FcWallEnt *wall;

    memset(&work, 0, sizeof(work));
    rv = 1;
    flag = actor ? GOBJ_SUB(actor)->disp : 0;
    work.radius = f;
    sceVu0CopyVector(work.pt[0], hand0);
    sceVu0CopyVector(work.pt[1], hand1);
    if (flag != 0) {
        GOBJ_SUB(actor)->disp = 0;
    }
    ClipWall(&work);
    if (flagout != 0) {
        *flagout = work.attr;
    }
    wall = work.wall.elem;
    if (wall == 0) {
        rv = 0;
    }
    if (posout != 0) {
        *(float *)((char *)posout + 0) = work.pt[2][0];
        *(float *)((char *)posout + 4) = work.pt[2][1];
        *(float *)((char *)posout + 8) = work.pt[2][2];
    }
    if (wall != 0 && magtarget != 0) {
        GetOrientOfWall(magtarget, wall, &work.wall.o);
    }
    if (flag != 0) {
        GOBJ_SUB(actor)->disp = 1;
    }
    return rv & 0xFF;
}

inline int ACTCheckCollis_CI(float *start, float *end, int *attr, WallCfg *wallHit)
{
    ClipWork buf;
    memset(&buf, 0, ICO_MAX_SIZE(ClipWork, 0xC0));
    buf.radius = 0;
    sceVu0CopyVector(buf.pt[0], start);
    sceVu0CopyVector(buf.pt[1], end);
    ClipWall(&buf);
    if (attr != 0) {
        *attr = buf.attr;
    }
    if (wallHit != 0) {
        wallHit->o = buf.wall.o;
        wallHit->elem = buf.wall.elem;
    }
    return buf.wall.elem != 0;
}

void *floorGObj_ACTCheckCollis_WELL;

void *wallGObj_ACTCheckCollis_WAY;

/* the float is the last parameter */
inline int ACTCheckCollis_WELL(void *p0, void *p1, void *actor, void *posout, float f)
{
    ClipWork work;
    int flag;
    int rv;

    memset(&work, 0, sizeof(work));
    rv = 1;
    flag = actor ? GOBJ_SUB(actor)->disp : 0;
    work.radius = f;
    floorGObj_ACTCheckCollis_WELL = 0;
    sceVu0CopyVector(work.pt[0], p0);
    sceVu0CopyVector(work.pt[1], p1);
    if (flag != 0) {
        GOBJ_SUB(actor)->disp = 0;
    }
    ClipFloor(&work);
    if (work.floor.elem == 0) {
        rv = 0;
    } else {
        floorGObj_ACTCheckCollis_WELL = work.floor.o.obj;
    }
    if (flag != 0) {
        GOBJ_SUB(actor)->disp = 1;
    }
    if (posout != 0) {
        *(float *)((char *)posout + 0) = work.pt[2][0];
        *(float *)((char *)posout + 4) = work.pt[2][1];
        *(float *)((char *)posout + 8) = work.pt[2][2];
    }
    return rv;
}

inline unsigned char ACTCheckCollis_WAY(float f, void *p0, void *p1, void *actor, void *posout)
{
    ClipWork work;
    int flag;
    int attr;

    memset(&work, 0, sizeof(work));
    flag = actor ? GOBJ_SUB(actor)->disp : 0;
    work.radius = f;
    wallGObj_ACTCheckCollis_WAY = 0;
    sceVu0CopyVector(work.pt[0], p0);
    sceVu0CopyVector(work.pt[1], p1);
    if (flag != 0) {
        GOBJ_SUB(actor)->disp = 0;
    }
    ClipWall(&work);
    attr = work.attr;
    if (flag != 0) {
        GOBJ_SUB(actor)->disp = 1;
    }
    /* the wall record is published on both paths */
    if (work.wall.elem == 0) {
        if (flag != 0) {
            GOBJ_SUB(actor)->disp = 0;
        }
        ClipWallField(&work);
        if (flag != 0) {
            GOBJ_SUB(actor)->disp = 1;
        }
        if (work.wall.elem == 0) {
            return 0;
        }
        wallGObj_ACTCheckCollis_WAY = work.wall.o.obj;
    } else {
        wallGObj_ACTCheckCollis_WAY = work.wall.o.obj;
    }
    if (CompareAttribute(attr, 0x30000) != 0) {
        return 0;
    }
    if (posout != 0) {
        *(float *)((char *)posout + 0) = work.pt[2][0];
        *(float *)((char *)posout + 4) = work.pt[2][1];
        *(float *)((char *)posout + 8) = work.pt[2][2];
    }
    return 1;
}

inline unsigned char ACTCheckCollis_VIEW(float f, void *p0, void *p1, void *actor)
{
    ClipWork work;
    int flag;
    int rv;

    memset(&work, 0, sizeof(work));
    rv = 1;
    flag = actor ? GOBJ_SUB(actor)->disp : 0;

    if (!(_DistSqGV((int *)p0, p1) < 25000000.0f)) {
        return 1;
    }

    work.radius = f;
    sceVu0CopyVector(work.pt[0], p0);
    sceVu0CopyVector(work.pt[1], p1);
    if (flag != 0) {
        GOBJ_SUB(actor)->disp = 0;
    }
    ClipWall(&work);
    if (work.wall.elem == 0) {
        ClipFloor(&work);
        if (work.floor.elem == 0) {
            rv = 0;
        }
    }
    if (flag != 0) {
        GOBJ_SUB(actor)->disp = 1;
    }
    return rv;
}

int ACTCheckView(GObj *self, void *target, void *targetPos, int range, float f)
{
    float pos[4];
    float v[4];
    float d[4];
    float *m;
    int n;

    n = GetSkeltonFocusNode(self, 35) << 6;
    m = (float *)(n + GOBJ_SUB(self)->nodeMtx);
    pos[0] = m[12];
    pos[1] = m[13];
    pos[2] = m[14];
    if (_DistGV(pos, targetPos) < f) {
        return 1;
    }
    if (range >= 360) {
        return 1;
    }
    /* each arm a three-component vector set; v[3] is zeroed on the next
       line */
    if (((struct GObj *)self)->kind == 4) {
        v[0] = 0.0f;
        v[1] = -1.0f;
        v[2] = 0.0f;
    } else {
        v[0] = 0.0f;
        v[1] = 1.0f;
        v[2] = 0.0f;
    }
    v[3] = 0.0f;
    sceVu0ApplyMatrix(v, (char *)GOBJ_SUB(self)->nodeMtx + n, v);
    sceVu0SubVector(d, targetPos, pos);
    if (0.8f < (v[1] < 0.0f ? -v[1] : v[1])) {
        v[0] = test_CURRENTORIENT(self)[0];
        v[1] = test_CURRENTORIENT(self)[1];
        v[2] = test_CURRENTORIENT(self)[2];
    }
    if (range / 2 < (_RotyGV(v, d) < 0 ? -_RotyGV(v, d) : _RotyGV(v, d))) {
        return 0;
    }
    return 1;
}

static int ACTCheckViewCl_jun26(GObj *self, void *target, void *targetPos, int range, float f)
{
    float pos[4];
    float *m;
    int n;

    if (((struct GObj *)self)->kind == 4) {
        return 1;
    }
    n = GetSkeltonFocusNode(self, 35) << 6;
    m = (float *)(n + GOBJ_SUB(self)->nodeMtx);
    pos[0] = m[12];
    pos[1] = m[13];
    pos[2] = m[14];
    if (ACTCheckView(self, target, targetPos, range, f) == 0) {
        return 0;
    }
    return ACTCheckCollis_VIEW(0.0f, pos, targetPos, target) == 0;
}

inline int ACTCheckViewCl(GObj *self, void *target, void *targetPos, int range, float f)
{
    if (ICO_JUN26) {
        return ACTCheckViewCl_jun26(self, target, targetPos, range, f);
    }
    float pos[4];
    float *m;
    int n;

    if (((struct GObj *)self)->kind == 4) {
        return 1;
    }
    n = GetSkeltonFocusNode(self, 35) << 6;
    m = (float *)(n + GOBJ_SUB(self)->nodeMtx);
    pos[0] = m[12];
    pos[1] = m[13];
    pos[2] = m[14];
    if (ACTCheckView(self, target, targetPos, range, f) == 0) {
        return 0;
    }
    return ACTCheckCollis_VIEW(0.0f, pos, targetPos, target) == 0;
}

inline int ACTCheckViewClDetail(GObj *self, void *target, void *targetPos, int range, float f)
{
    float pos[4];
    float *m;
    int n;
    int ret;

    if (((struct GObj *)self)->kind == 4) {
        return 1;
    }
    n = GetSkeltonFocusNode(self, 35) << 6;
    m = (float *)(n + GOBJ_SUB(self)->nodeMtx);
    pos[0] = m[12];
    pos[1] = m[13];
    pos[2] = m[14];
    ret = ACTCheckView(self, target, targetPos, range, f);
    if (ACTCheckCollis_VIEW(0.0f, pos, targetPos, target)) {
        return 0;
    }
    if (ret != 0) {
        return 1;
    }
    return 2;
}

static inline void ACTGameView_Add_jun26(GObj *self, GObj *target)
{
    int n = actGameView.num++;
    if (n >= 100) {
        debug_StdPrintfDummy("too many view check object");
        debug_assert("src/act-game.c", 1465);
        __assert("src/act-game.c", 1465, "0");
    }
    actGameView.obj[n] = target;
    actGameView.view[n] = 0;
}

/* actGameView is one table: a 100-entry object list at +0x000, two parallel
   100-entry int arrays at +0x190 and +0x320, and the entry count at +0x4B0. */
inline void ACTGameView_Add(GObj *self, GObj *target)
{
    if (ICO_JUN26) {
        ACTGameView_Add_jun26(self, target);
        return;
    }
    int n = actGameView.num++;
    if (n >= 100) {
        debug_StdPrintfDummy("too many view check object");
        debug_assert("src/act-game.c", LN(1791, 1788, 1788, 1637, 1604, 1604, 1788));
        __assert("src/act-game.c", LN(1791, 1788, 1788, 1637, 1604, 1604, 1788), "0");
    }
    actGameView.obj[n] = target;
    actGameView.view[n] = 0;
    actGameView.simple[n] = 0;
}

inline void ACTGameView_Init(void)
{
    actGameView.num = 0;
    actGameView.cur = 0;
}

inline void ACTGameView_FirstSet(char *self)

{
    GObj *g;

    g = isysGObjSearchFromObjKindID_begin(4);
    while (g != 0) {
        ACTGameView_Add((GObj *)g, (GObj *)g);
        g = isysGObjSearchFromObjKindID_next(g);
    }
}

static void ACTGameView_Loop_jun26(GObj *self)
{
    float pos[4];
    GObj *obj;
    int i;

    i = actGameView.cur++;
    if (!(actGameView.cur < actGameView.num)) {
        actGameView.cur = 0;
    }
    obj = actGameView.obj[i];
    if (obj != 0) {
        if (!(5000.0f < _DistGV(test_CURRENTROOT(self), test_CURRENTROOT(obj)))) {
            GetRootPosition(pos, obj);
            actGameView.view[i] = ACTCheckViewCl(self, obj, pos, 150, 300.0f);
        } else {
            actGameView.view[i] = 0;
        }
    } else {
        actGameView.view[i] = 0;
    }
}

/* The view work record is reached as `self->act->view`, its two chase loads
   read as int. */
void ACTGameView_Loop(GObj *self)
{
    if (ICO_JUN26) {
        ACTGameView_Loop_jun26(self);
        return;
    }
    float pos[4];
    int i;

    i = actGameView.cur;
    if (actGameView.simple[i] != 0) {
        GetRootPosition(pos, actGameView.obj[i]);
        actGameView.view[i] = ACTCheckView(self, actGameView.obj[i], pos, 150, 300.0f);
    } else {
        actGameView.view[i] = 0;
    }

    switch (GOBJ_WORK(self)->viewState) {
    case 0:
        if (5000.0f < _DistGV(test_CURRENTROOT(self), test_CURRENTROOT(actGameView.obj[i]))) {
            GOBJ_WORK(self)->viewState = 6;
        } else {
            GOBJ_WORK(self)->viewState = 1;
        }
        break;
    case 1:
        GOBJ_WORK(self)->view.func = ClipWall;
        GOBJ_WORK(self)->view.obj = actGameView.obj[i];
        GOBJ_WORK(self)->view.clip.radius = 0.0f;
        GetSkeltonPosition(GOBJ_WORK(self)->view.clip.pt[0], self, PAL_NTSCAUG6JUN26(0x23, 35));
        GetRootPosition(GOBJ_WORK(self)->view.clip.pt[1], actGameView.obj[i]);
        RequestClipCollision(&GOBJ_WORK(self)->view);
        GOBJ_WORK(self)->viewState = 2;
        break;
    case 2:
        if (GOBJ_WORK(self)->view.done != 0) {
            if (GOBJ_WORK(self)->view.clip.wall.elem != 0) {
                GOBJ_WORK(self)->viewState = 6;
            } else {
                GOBJ_WORK(self)->viewState = 3;
            }
        }
        break;
    case 3:
        GOBJ_WORK(self)->view.func = ClipFloor;
        GOBJ_WORK(self)->view.obj = actGameView.obj[i];
        GOBJ_WORK(self)->view.clip.radius = 0.0f;
        GetSkeltonPosition(GOBJ_WORK(self)->view.clip.pt[0], self, PAL_NTSCAUG6JUN26(0x23, 35));
        GetRootPosition(GOBJ_WORK(self)->view.clip.pt[1], actGameView.obj[i]);
        RequestClipCollision(&GOBJ_WORK(self)->view);
        GOBJ_WORK(self)->viewState = 4;
        break;
    case 4:
        if (GOBJ_WORK(self)->view.done != 0) {
            if (GOBJ_WORK(self)->view.clip.floor.elem != 0) {
                GOBJ_WORK(self)->viewState = 6;
            } else {
                GOBJ_WORK(self)->viewState = 5;
            }
        }
        break;
    case 5:
        actGameView.simple[i] = 1;
        GOBJ_WORK(self)->viewState = 7;
        break;
    case 6:
        actGameView.simple[i] = 0;
        GOBJ_WORK(self)->viewState = 7;
        break;
    case 7:
        actGameView.cur++;
        if (!(actGameView.cur < actGameView.num)) {
            actGameView.cur = 0;
        }
        GOBJ_WORK(self)->viewState = 0;
        break;
    }
}

inline int ACTGameView_Check(GObj *self, GObj *obj)
{
    int i;
    for (i = 0; i < actGameView.num; i++) {
        if (actGameView.obj[i] == obj) {
            return *(unsigned char *)&actGameView.view[i];
        }
    }
    return 0;
}

inline int ACTGameViewSimple_Check(GObj *self, GObj *obj)
{
    int i;
    for (i = 0; i < actGameView.num; i++) {
        if (actGameView.obj[i] == obj) {
            return *(unsigned char *)&actGameView.simple[i];
        }
    }
    return 0;
}

static void ACTGame_LwsEffectProcess(GObj *self)
{
    BgaDisp *m = GOBJ_ACT(self)->enemy->lwsEffect;
    if (m != 0) {
        DispMultiBgaManagerWithKind(PAL_JPNKORCN_NTSC_AUG6_JUN26(0x1F8, 500, 433, 432, 396), m, 1);
    }
}

inline void ACTGame_LwsEffectInit(GObj *self)
{
    GOBJ_ACT(self)->enemy->lwsEffect = InitMultiBgaManager(1);
}

inline void ACTGame_LwsEffect_Guard(GObj *self)
{
    float q[4];
    float v[4];
    EnemyBattleWork *p;

    _OrientXZGV(v, test_CURRENTROOT(boyGObj), test_CURRENTROOT(self));
    ActGame_GetOrientQ(q, v, 0);

    stage_SetLoopFlag(PAL_JPNKORCN_NTSC_AUG6_JUN26(504, 500, 433, 432, 396), 0);
    stage_SetFrameStep(PAL_JPNKORCN_NTSC_AUG6_JUN26(0x1F8, 500, 433, 432, 396), 1);
    p = GOBJ_ACT(self)->enemy;
    EntryMultiBgaManager(p->lwsEffect, 0, -1, test_CURRENTROOT(self), q);
}

inline void ActGame_GetOrientQ(void *q, void *v, int deg)
{
    float tmp[4];
    int n;

    sceVu0ScaleVector(tmp, v, -1.0f);
    n = (int)(_GetDirection(tmp) / 3.1415927f * 180.0f) + deg;
    SetIdentityQuaternion(q);
    RotQuaternionY(q, (short)(n * 32768 / 180));
}

inline void ACTCharctrl_Lock(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    s->flags18.ll &= ~(PAL_NTSCAUG6_JUN26(1ULL << 48, 1ULL << 43, 1ULL << 42));
    s->flags18.ll &= ~(PAL_NTSCAUG6_JUN26(1ULL << 49, 1ULL << 44, 1ULL << 43));
}

inline void ACTCharctrl_Unlock(GObj *self)
{
    Act *p = GOBJ_ACT(self);
    p->flags18.ll |= (PAL_NTSCAUG6_JUN26(1ULL << 48, 1ULL << 43, 1ULL << 42));
    p->flags18.ll |= (PAL_NTSCAUG6_JUN26(1ULL << 49, 1ULL << 44, 1ULL << 43));
}

inline GObj *ACTGame_GetNearestGObj(float *pos, int kind)
{
    float best_val = 3.40282347e+38f; /* FLT_MAX */
    GObj *best = 0;
    GObj *node;

    node = isysGObjSearchFromObjKindID_begin(kind);
    if (node != 0) {
        do {
            float val = _DistSqGV(test_CURRENTROOT(node), pos);
            if (val < best_val) {
                best_val = val;
                best = node;
            }
            node = isysGObjSearchFromObjKindID_next(node);
        } while (node != 0);
    }
    return best;
}

inline void _GetRootObjectOrient(void *orient, GObj *obj)
{
    float v[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    sceVu0ApplyMatrix(orient, (void *)GOBJ_SUB(obj)->nodeMtx, v);
}

inline ICO_WORD_PTR(GObj *) ACTGame_isWeaponEnableCatchfire(GObj *self)
{
    ICO_WORD_PTR(GObj *) ret = 0;
    unsigned long long combustible = ACTGame_isWeaponCombustible(self);
    if (combustible) {
        ret = GetTorchGObjOfWeapon(self);
    }
    return ret;
}

inline GObj *ACTGame_isHangChain(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    if (self == boyGObj) {
        const ActModeRec *attr = &actModeTbl[s->actMode];
        if (attr->onChain) {
            return s->chain;
        }
    }
    return 0;
}

inline int ACTGame_GetMotOrientFromWeapon(GObj *weapon)
{
    int rv;
    if (weapon != 0) {
        rv = weaponKind[CheckWeaponKind(weapon)].motOrient;
    } else {
        rv = 0;
    }
    return rv;
}

inline unsigned char ACTGame_NoWeapon(GObj *self)
{
    GObj *w = GOBJ_ACT(self)->weapon;
    unsigned char r = 0;
    if (w == 0 || CheckWeaponKind(w) == 0)
        r = 1;
    return r;
}

/* The retail binary reaches CheckWeaponKind with the caller's `self` still in
   a0 (ACTGame_isWeaponEnableCatchfire's argument); the port passes it
   explicitly. */

inline int ACTGame_isWeaponCombustible(GObj *self)
{
    return CheckWeaponKind(self) == 1;
}

/* both absolute values are written as `((x) < 0 ? -(x) : (x))` */
int _ACTGame_SearchGObj(GObj *self, GObj *tgt, float range, float height, int angle, float *out)
{
    float buf[4];
    int n;

    if (!(_DistxzSqGV(test_CURRENTROOT(self), test_CURRENTROOT(tgt)) < range * range)) {
        return 0;
    }
    if ((test_CURRENTROOT(self)[1] - test_CURRENTROOT(tgt)[1] < 0.0f
             ? -(test_CURRENTROOT(self)[1] - test_CURRENTROOT(tgt)[1])
             : test_CURRENTROOT(self)[1] - test_CURRENTROOT(tgt)[1]) < height) {
        sceVu0SubVector(buf, test_CURRENTROOT(tgt), test_CURRENTROOT(self));
        n = _RotyGV(buf, test_CURRENTORIENT(self)) < 0 ? -_RotyGV(buf, test_CURRENTORIENT(self))
                                                       : _RotyGV(buf, test_CURRENTORIENT(self));
        if (n < angle) {
            out[0] = buf[0];
            out[1] = buf[1];
            out[2] = buf[2];
            return 1;
        }
    }
    return 0;
}

inline void ACTLookTarget_Init(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    s->lookTarget = 0;
    s->lookMode = 0;
    s->lookPri = 0;
}

inline int _ACTLookTarget_Set(GObj *self, GObj *target, float *pos, int pri, int mode)
{
    Act *s = GOBJ_ACT(self);
    int ret = 0;

    if (pri == 6) {
        ACTLookTarget_Init(self);
    } else if (pri >= s->lookPri) {
        s->lookTarget = target;
        if (pos != 0) {
            s->lookPosX = pos[0];
            s->lookPosY = pos[1];
            s->lookPosZ = pos[2];
        }
        s->lookMode = mode;
        s->lookPri = pri;
        ret = 1;
    }
    return ret;
}

int ACTLookTarget_Exec(GObj *self)
{
    float pos[4];
    Act *s = GOBJ_ACT(self);
    GObj *t = s->lookTarget;
    int rv;
    int b0;

    if (debug_font_flag & 1) {
        debug_Printf(10, 170, 0x0FFFFFFF, "mode=[%d]\n", GOBJ_SUB(self)->root.lookMode);
    }
    rv = 0;
    if (s->lookPri == 0) {
        GOBJ_SUB(self)->root.lookMode = 0;
    } else {
        if (t == 0) {
            pos[0] = s->lookPosX;
            pos[1] = s->lookPosY;
            pos[2] = s->lookPosZ;
        } else if (t == boyGObj) {
            /* the boy's skeleton position read in place */
            int idx = GetSkeltonFocusNode(t, 35) << 6;
            pos[0] = *(float *)(idx +
                                ICO_RAW(int, ((IntFloat *)&t->dobj)->i, 0xC, GOBJ_SUB(t)->nodeMtx) +
                                0x30);
            pos[1] = *(float *)(idx +
                                ICO_RAW(int, ((IntFloat *)&t->dobj)->i, 0xC, GOBJ_SUB(t)->nodeMtx) +
                                0x34);
            pos[2] = *(float *)(idx +
                                ICO_RAW(int, ((IntFloat *)&t->dobj)->i, 0xC, GOBJ_SUB(t)->nodeMtx) +
                                0x38);
        } else {
            GetRootPosition(pos, t);
        }
        b0 = s->lookMode;
        rv = 1;
        ((IntFloat *)GOBJ_SUB(self)->root.lookPos)->f = pos[0];
        ((IntFloat *)((char *)&GOBJ_SUB(self)->root.lookPos + 4))->f = pos[1];
        ((IntFloat *)((char *)&GOBJ_SUB(self)->root.lookPos + 8))->f = pos[2];
        GOBJ_SUB(self)->root.lookMode = b0;
    }
    return rv;
}

inline void ACTParaStatus_Init(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    ActPara_InitSystem();
    ACTParaStatus_Clear(self);
    ActPara_MakeTbl(GOBJ_WORK(self)->paraTbl, s->paraStatus, 0);
    s->lastParaStatus = s->paraStatus;
}

void ACTParaStatus_Clear(GObj *self)
{
    GOBJ_ACT(self)->paraStatus = 0;
    _ACTParaStatus_Set(self, 0);
}

inline void _ACTParaStatus_Set(GObj *self, int bit)
{
    Act *s = GOBJ_ACT(self);
    s->paraStatus |= PAL_NTSCAUG6JUN26((1ULL << bit) & ~(unsigned long long)s->flags, 1 << bit);
}

inline unsigned long long _ACTParaStatus_Check(GObj *self, int bit)
{
    Act *s = GOBJ_ACT(self);
    return PAL_NTSCAUG6JUN26(((unsigned long long)s->paraStatus >> bit), (s->paraStatus >> bit)) &
           1;
}

static void ACTParaStatus_Exec_ntsc(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    Sub15C *sub;
    int changed;

    changed = 0;
    if ((int)(s->flags20.ll >> 6) & 1) {
        _ACTParaStatus_Set(self, 4);
    }
    if (s->paraStatus != s->lastParaStatus) {
        s->lastParaStatus = s->paraStatus;
        changed = 1;
    }
    if ((GOBJ_ACT(self)->enemy->paraTimer)++ >= 121) {
        sub = GOBJ_SUB(self);
        if ((sub->ctrl.ctrlFlags & 0x16) || (int)sub->ctrl.frameEnd != 0) {
            if (motionKind[sub->ctrl.motion].playMode == 1) {
                GOBJ_ACT(self)->enemy->paraTimer = 0;
                GOBJ_ACT(self)->enemy->paraRandom = (int)(_GetRandom() * 10.0f);
                changed = 1;
            }
        }
    }
    _ACTParaStatus_Set(self, 1);
    if (changed == 0) {
        return;
    }
    ActPara_MakeTbl(GOBJ_WORK(self)->paraTbl, s->paraStatus, GOBJ_ACT(self)->enemy->paraRandom);
    SetParallelMotionTable(self, GOBJ_WORK(self)->paraTbl, ActPara_GetDefTbl(), 0, 3);
}

static void ACTParaStatus_Exec_aug6jun26(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    Sub15C *sub;
    int changed;

    changed = 0;
    if (((s->flags20.ll & (PAL_JUN26(1ULL << 3, 1ULL << 1))) != 0)) {
        _ACTParaStatus_Set(self, 4);
    }
    if (s->paraStatus != s->lastParaStatus) {
        s->lastParaStatus = s->paraStatus;
        changed = 1;
    }
    if ((GOBJ_ACT(self)->enemy->paraTimer)++ >= 121) {
        sub = GOBJ_SUB(self);
        if ((sub->ctrl.ctrlFlags & 0x16) || (int)sub->ctrl.frameEnd != 0) {
            if (motionKind[sub->ctrl.motion].playMode == 1) {
                GOBJ_ACT(self)->enemy->paraTimer = 0;
                GOBJ_ACT(self)->enemy->paraRandom = (int)(_GetRandom() * 10.0f);
                changed = 1;
            }
        }
    }
    _ACTParaStatus_Set(self, 1);
    if (changed == 0) {
        return;
    }
    ActPara_MakeTbl(GOBJ_WORK(self)->paraTbl, s->paraStatus, GOBJ_ACT(self)->enemy->paraRandom);
    SetParallelMotionTable(self, GOBJ_WORK(self)->paraTbl, ActPara_GetDefTbl(), 0, 3);
}

void ACTParaStatus_Exec(GObj *self)
{
    if (ICO_NTSC) {
        ACTParaStatus_Exec_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        ACTParaStatus_Exec_aug6jun26(self);
        return;
    }
    Act *s = GOBJ_ACT(self);
    Sub15C *sub;
    EnemyBattleWork *p;
    int changed;

    changed = 0;
    if ((int)(s->flags20.ll >> 16) & 1) {
        _ACTParaStatus_Set(self, 42);
    }
    if ((int)(s->flags20.ll >> 17) & 1) {
        _ACTParaStatus_Set(self, 43);
    }
    if ((unsigned long long)s->paraStatus != s->lastParaStatus) {
        s->lastParaStatus = (unsigned long long)s->paraStatus;
        changed = 1;
    }
    p = s->enemy;
    if ((p->paraTimer)++ >= 121) {
        sub = GOBJ_SUB(self);
        if ((sub->ctrl.ctrlFlags & 0x16) || (int)sub->ctrl.frameEnd != 0) {
            if (motionKind[sub->ctrl.motion].playMode == 1) {
                GOBJ_ACT(self)->enemy->paraTimer = 0;
                GOBJ_ACT(self)->enemy->paraRandom = (int)(_GetRandom() * 10.0f);
                changed = 1;
            }
        }
    }
    _ACTParaStatus_Set(self, 1);
    if (changed == 0) {
        return;
    }
    ActPara_MakeTbl(GOBJ_WORK(self)->paraTbl, s->paraStatus, GOBJ_ACT(self)->enemy->paraRandom);
    SetParallelMotionTable(self, GOBJ_WORK(self)->paraTbl, ActPara_GetDefTbl(), 0,
                           (int)GOBJ_WORK(self)->parallelInterp);
}

inline void _ACTCharStatus_Init(int **self)
{
    /* self[0x59] is the act word at GObj + 0x164 only with 4-byte words */
    Act *a = GOBJ_ACT(self);

    a->bits58 = 0;
    a->bits60 = 0;
}

static inline void _ACTCharStatus_Clear_jun26(void *self)
{
    Act *s = GOBJ_ACT(self);
    ICO_WORD_PTR(GObj *) old = s->statusOther;
    GObj *sel;
    GObj *g;
    float nearest;
    float d;

    /* the EE clears 0x38 bytes, bits58 up to paraStatus; the host record
       is wider there (pointer-wide words), so clear the same members */
    memset((char *)&s->bits58, 0,
           __builtin_offsetof(Act, paraStatus) - __builtin_offsetof(Act, bits58));
    if (self == (char *)boyGObj || self == ((char *)girlGObj)) {
        nearest = 3.40282347e+38f; /* FLT_MAX */
        sel = 0;
        g = isysGObjSearchFromObjKindID_begin(4);
        while (g != 0) {
            d = _DistGV(test_CURRENTROOT(self), test_CURRENTROOT((GObj *)g));
            if (actEnemyFlagCheckActive((GObj *)g)) {
                if (d < nearest) {
                    sel = g;
                    nearest = d;
                }
            }
            g = isysGObjSearchFromObjKindID_next((GObj *)g);
        }
        s->statusOther = (ICO_WORD_PTR(GObj *))sel;
    }
}

void _ACTCharStatus_Clear(void *self)
{
    if (ICO_JUN26) {
        _ACTCharStatus_Clear_jun26(self);
        return;
    }
    Act *s = GOBJ_ACT(self);
    ICO_WORD_PTR(GObj *) old = s->statusOther;
    GObj *sel;
    GObj *g;
    float nearest;
    float d;

    /* the EE clears 0x38 bytes, bits58 up to paraStatus; the host record
       is wider there (pointer-wide words), so clear the same members */
    memset((char *)&s->bits58, 0,
           __builtin_offsetof(Act, paraStatus) - __builtin_offsetof(Act, bits58));
    if (self == (char *)boyGObj || self == ((char *)girlGObj)) {
        nearest = 3.40282347e+38f; /* FLT_MAX */
        sel = 0;
        g = isysGObjSearchFromObjKindID_begin(4);
        while (g != 0) {
            d = _DistGV(test_CURRENTROOT(self), test_CURRENTROOT((GObj *)g));
            if (actEnemyFlagCheckActive((GObj *)g)) {
                if (d < nearest) {
                    sel = g;
                    nearest = d;
                }
            }
            g = isysGObjSearchFromObjKindID_next((GObj *)g);
        }
        s->statusOther = (ICO_WORD_PTR(GObj *))sel;
        if (self == (char *)boyGObj) {
            if (old != 0 && s->frame % ((60 - systemStatus[0] * 10) / systemStatus[1] * 2) != 0) {
                s->statusOther = old;
            }
        }
    }
}

static inline void _ACTCharStatus_Set_jun26(GObj *self, int bit, float f, ICO_WORD val)
{
    Act *s = GOBJ_ACT(self);

    s->bits58 |= 1LL << bit;

    switch (bit) {
    case 6:
        s->statusWait8 = f;
        break;

    case 3:
        s->statusWait5 = f;
        break;

    case 13:
        s->statusVal17 = f;
        break;

    case 14:
        s->statusVal18 = val;
        break;

    case 8:
        s->statusTarget = (ICO_WORD_PTR(GObj *))val;
        break;

    case 0:
        s->statusOther = (ICO_WORD_PTR(GObj *))val;
        break;

    case 9:
        s->statusObj = (ICO_WORD_PTR(GObj *))val;
        break;
    }
}

static inline void _ACTCharStatus_Set_aug6(GObj *self, int bit, float f, ICO_WORD val)
{
    Act *s = GOBJ_ACT(self);

    s->bits58 |= 1LL << bit;

    switch (bit) {
    case 8:
        s->statusWait8 = f;
        break;

    case 5:
        s->statusWait5 = f;
        break;

    case 15:
        s->statusVal17 = f;
        break;

    case 16:
        s->statusVal18 = val;
        break;

    case 10:
        s->statusTarget = (ICO_WORD_PTR(GObj *))val;
        break;

    case 2:
        s->statusOther = (ICO_WORD_PTR(GObj *))val;
        break;

    case 11:
        s->statusObj = (ICO_WORD_PTR(GObj *))val;
        break;
    }
}

static inline void _ACTCharStatus_Set_ntsc(GObj *self, int bit, float f, ICO_WORD val)
{
    Act *s = GOBJ_ACT(self);

    s->bits58 |= 1 << bit;

    switch (bit) {
    case 8:
        s->statusWait8 = f;
        break;

    case 5:
        s->statusWait5 = f;
        break;

    case 15:
        s->statusVal17 = f;
        break;

    case 16:
        s->statusVal18 = val;
        break;

    case 10:
        s->statusTarget = (ICO_WORD_PTR(GObj *))val;
        break;

    case 2:
        s->statusOther = (ICO_WORD_PTR(GObj *))val;
        break;

    case 11:
        s->statusObj = (ICO_WORD_PTR(GObj *))val;
        break;
    }
}

inline void _ACTCharStatus_Set(GObj *self, int bit, float f, ICO_WORD val)
{
    if (ICO_JUN26) {
        _ACTCharStatus_Set_jun26(self, bit, f, val);
        return;
    }
    if (ICO_NTSC) {
        _ACTCharStatus_Set_ntsc(self, bit, f, val);
        return;
    }
    if (ICO_AUG6) {
        _ACTCharStatus_Set_aug6(self, bit, f, val);
        return;
    }
    Act *s = GOBJ_ACT(self);

    s->bits58 |= 1LL << bit;

    switch (bit) {
    case 8:
        s->statusWait8 = f;
        break;

    case 5:
        s->statusWait5 = f;
        break;

    case 17:
        s->statusVal17 = f;
        break;

    case 18:
        s->statusVal18 = val;
        break;

    case 10:
        s->statusTarget = (ICO_WORD_PTR(GObj *))val;
        break;

    case 2:
        s->statusOther = (ICO_WORD_PTR(GObj *))val;
        break;

    case 11:
        s->statusObj = (ICO_WORD_PTR(GObj *))val;
        break;
    }
}

static inline unsigned char _ACTCharStatus_Check_ntsc(GObj *self, int bit)
{
    Act *s = GOBJ_ACT(self);

    if ((char *)s != 0) {
        if (ICO_RAW(unsigned long long, s, 0x58, s->bits58) & (1 << bit)) {
            return 1;
        }
    }
    return 0;
}

static inline unsigned char _ACTCharStatus_Check_aug6jun26(GObj *self, int bit)
{
    Act *s = GOBJ_ACT(self);

    if ((char *)s != 0) {
        if ((ICO_RAW(int, s, 0x58, s->bits58) >> bit) & 1) {
            return 1;
        }
    }
    return 0;
}

inline unsigned char _ACTCharStatus_Check(GObj *self, int bit)
{
    if (ICO_NTSC) {
        return _ACTCharStatus_Check_ntsc(self, bit);
    }
    if (ICO_AUG6JUN26) {
        return _ACTCharStatus_Check_aug6jun26(self, bit);
    }
    Act *s = GOBJ_ACT(self);
    int r;

    if ((char *)s != 0) {
        r = (ICO_RAW(unsigned long long, s, 0x58, s->bits58) >> bit) & 1;
        if (r != 0) {
            return 1;
        }
    }
    return 0;
}

inline void _ACTCharStatus_Exec(void) {}

inline void ACTGame_SetMotionPlaySpeedRatio_Clear(GObj *self)
{
    EnemyBattleWork *p = GOBJ_ACT(self)->enemy;
    p->speedRatio = 1.0f;
    *(MpsrMode *)&p->speedRatioPri = MPSR_OFF;
}

inline void ACTGame_SetMotionPlaySpeedRatio_Reserve(GObj *self, float ratio, unsigned int pri)
{
    EnemyBattleWork *p = GOBJ_ACT(self)->enemy;
    if (p->speedRatioPri <= pri) {
        p->speedRatio = ratio;
        p->speedRatioPri = pri;
    }
}

/* The play-speed ratio's debug override, built only when DEBUG is defined:
   the debug build can fix the ratio from the debugger before it is applied;
   the retail build leaves the helper without a body. */
static __inline__ void speedRatioDebugOverride(float *ratio) /* derived name */
{
#ifdef DEBUG
    if (dbgSpeedRatioFix) {
        *ratio = dbgSpeedRatio;
    }
#endif
}

/* PC port: port/game/options.c: [gameplay] shadow_speed, the factor 0.75 to 1.5 */
extern float ico_opt_shadow_speed_factor(void);

inline void ACTGame_SetMotionPlaySpeedRatio_Exec(GObj *self)
{
    float ratio;
    int keep;
    int diveMotion;

    ratio = 1.0f;
    keep = (unsigned int)(int)GOBJ_ACT(self)->enemy->speedRatioPri < 3 && self == girlGObj;
    if ((int)GOBJ_ACT(self)->enemy->speedRatioPri == 1) {
        if (((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags >> 30) & 1) {
            ratio = GOBJ_ACT(self)->enemy->speedRatio;
            keep = 0;
        }
    } else {
        if ((((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags >> 26) & 1) == 0) {
            ratio = GOBJ_ACT(self)->enemy->speedRatio;
        }
    }
    if (keep) {
        ratio = 1.0f;
    }
    /* PC port: [gameplay] shadow_speed (port/game/options.c) scales a shadow's walking,
       running and flying; the ratio multiplies the motion's play speed, and the ground speed
       is the motion's root motion, so both scale in step.  Left alone: a frozen shadow (the
       0.0001 freeze) and the pit dive (this disc's kidnap-end motion), which [gameplay]
       pit_time owns. */
    diveMotion = ICO_JUN26 ? 938 : ICO_NTSC ? 903 : ICO_AUG6 ? 905 : 952;
    if (((struct GObj *)self)->kind == 4 && ratio > 0.001f &&
        ico_opt_shadow_speed_factor() != 1.0f && GOBJ_SUB(self)->ctrl.motion != diveMotion) {
        ratio *= ico_opt_shadow_speed_factor();
    }
    speedRatioDebugOverride(&ratio);
    SetMotionPlaySpeedRatio(self, ratio);
}

inline void SetDirectRootPositionWithNodePointLimit(void *self, int node, void *pos, float t,
                                                    float limit)
{
    float buf0[4];
    float buf18[4];
    float buf16[4];

    GetSkeltonPosition(buf0, self, node);
    sceVu0SubVector(buf16, pos, buf0);
    if (limit < FSqrt(buf16[0] * buf16[0] + buf16[1] * buf16[1] + buf16[2] * buf16[2])) {
        sceVu0Normalize(buf16, buf16);
        sceVu0ScaleVector(buf16, buf16, limit);
        sceVu0AddVector(buf18, buf0, buf16);
        if (0.0f < buf18[1] - *(float *)((char *)pos + 4)) {
            buf18[1] = *(float *)((char *)pos + 4);
        }
        SetDirectRootPositionNoFittingWithNodePoint(self, node, buf18, 1.0f);
        return;
    }
    SetDirectRootPositionNoFittingWithNodePoint(self, node, pos, t);
}

void GetSkeltonOrient(float *out, void *obj, int node)
{
    int n = GetSkeltonFocusNode(obj, node);
    if (((GObj *)obj)->kind == 4) {
        *(int *)((char *)out + 0x0) = 0;
        ((IntFloat *)((char *)out + 0x4))->f = -1.0f;
        *(int *)((char *)out + 0x8) = 0;
    } else {
        *(int *)((char *)out + 0x0) = 0;
        ((IntFloat *)((char *)out + 0x4))->f = 1.0f;
        *(int *)((char *)out + 0x8) = 0;
    }
    *(int *)((char *)out + 0xC) = 0;
    sceVu0ApplyMatrix(out, (char *)(GOBJ_SUB(obj)->nodeMtx + (n << 6)), out);
}

inline void GetSkeltonPosition(float *dst, GObj *obj, int node)
{
    int idx = GetSkeltonFocusNode(obj, node) << 6;
    dst[0] = *(float *)(idx +
                        ICO_RAW(int, ((IntFloat *)&((struct GObj *)obj)->dobj)->i, 0xC,
                                GOBJ_SUB(obj)->nodeMtx) +
                        0x30);
    dst[1] = *(float *)(idx +
                        ICO_RAW(int, ((IntFloat *)&((struct GObj *)obj)->dobj)->i, 0xC,
                                GOBJ_SUB(obj)->nodeMtx) +
                        0x34);
    dst[2] = *(float *)(idx +
                        ICO_RAW(int, ((IntFloat *)&((struct GObj *)obj)->dobj)->i, 0xC,
                                GOBJ_SUB(obj)->nodeMtx) +
                        0x38);
}

/* the bird broadcast, between GetSkeltonPosition and
   ACTGame_InnerVelocityUpdate */
static inline void actGame_SendMailToBirds(GObj *self) /* derived name */
{
    float other[4];
    float mine[4];
    GObj *bird;
    int mail;

    Act *s = GOBJ_ACT(self);
    bird = isysGObjSearchFromObjKindID_begin(32);
    mail = PAL_NTSC_AUG6_JUN26(423, 393, 389, 373);
    if (s->actMode == 3) {
        mail = PAL_NTSC_AUG6_JUN26(424, 394, 390, 374);
    }
    GetRootPosition(mine, self);
    while (bird != 0) {
        GetRootPosition(other, bird);
        if (_DistSqGV((int *)mine, other) < 160000.0f) {
            _ACTSendMailToBird(bird, mail, self);
        }
        bird = isysGObjSearchFromObjKindID_next(bird);
    }
}

static void ACTGame_InnerVelocityUpdate_jun26(GObj *self)
{
    float pos[4];
    int slow;
    int stop;
    ActWork *w;
    float speed;

    slow = 0;
    stop = 0;
    pos[0] = test_CURRENTROOT(self)[0];
    pos[1] = test_CURRENTROOT(self)[1];
    pos[2] = test_CURRENTROOT(self)[2];
    sceVu0SubVector((char *)&GOBJ_WORK(self)->velX, pos, (char *)&GOBJ_WORK(self)->lastPosX);
    speed = FSqrt(GOBJ_WORK(self)->velX * GOBJ_WORK(self)->velX +
                  GOBJ_WORK(self)->velY * GOBJ_WORK(self)->velY +
                  GOBJ_WORK(self)->velZ * GOBJ_WORK(self)->velZ);
    w = GOBJ_WORK(self);
    w->speed = speed;
    w->lastPosX = pos[0];
    w->lastPosY = pos[1];
    w->lastPosZ = pos[2];
    if (speed < 6.0f) {
        slow = 1;
    }
    if (speed < 2.0f) {
        stop = 1;
    }
    if (slow != 0) {
        w->slowFrames += 1;
    } else {
        w->slowFrames = 0;
    }
    if (stop != 0) {
        GOBJ_WORK(self)->stopFrames += 1;
    } else {
        GOBJ_WORK(self)->stopFrames = 0;
    }
    if (4 <= GOBJ_WORK(self)->slowFrames) {
        ((ActStatusWord *)&GOBJ_WORK(self)->stopFrames)->q |= 1ULL << 32;
    } else {
        ((ActStatusWord *)&GOBJ_WORK(self)->stopFrames)->q &= ~(1ULL << 32);
    }
    if (4 <= GOBJ_WORK(self)->stopFrames) {
        ((ActStatusWord *)&GOBJ_WORK(self)->stopFrames)->q |= 1ULL << 33;
    } else {
        ((ActStatusWord *)&GOBJ_WORK(self)->stopFrames)->q &= ~(1ULL << 33);
    }
}

/* The speed is computed before the parameter block is fetched, both hops of
   that fetch going through one pointer variable; the pos stores go through
   it and the two thresholds test the speed itself. */
static void ACTGame_InnerVelocityUpdate(GObj *self)
{
    if (ICO_JUN26) {
        ACTGame_InnerVelocityUpdate_jun26(self);
        return;
    }
    float pos[4];
    int slow;
    int stop;
    int nomove;
    char *p;
    float speed;

    slow = 0;
    stop = 0;
    pos[0] = test_CURRENTROOT(self)[0];
    pos[1] = test_CURRENTROOT(self)[1];
    pos[2] = test_CURRENTROOT(self)[2];
    sceVu0SubVector((char *)&GOBJ_WORK(self)->velX, pos, (char *)&GOBJ_WORK(self)->lastPosX);
    speed = FSqrt(GOBJ_WORK(self)->velX * GOBJ_WORK(self)->velX +
                  GOBJ_WORK(self)->velY * GOBJ_WORK(self)->velY +
                  GOBJ_WORK(self)->velZ * GOBJ_WORK(self)->velZ);
    p = (char *)GOBJ_ACT(self);
    p = ICO_RAW(char *, p, 0x688, (char *)GOBJ_ACT(self)->work);
    ICO_RAW(float, p, 0x440, GOBJ_WORK(self)->speed) = speed;
    ICO_RAW(float, p, 0x420, GOBJ_WORK(self)->lastPosX) = pos[0];
    ICO_RAW(float, p, 0x424, GOBJ_WORK(self)->lastPosY) = pos[1];
    ICO_RAW(float, p, 0x428, GOBJ_WORK(self)->lastPosZ) = pos[2];
    if (speed < 6.0f) {
        slow = 1;
    }
    if (speed < 2.0f) {
        stop = 1;
    }
    if (slow != 0) {
        GOBJ_WORK(self)->slowFrames += 1;
    } else {
        GOBJ_WORK(self)->slowFrames = 0;
    }
    if (stop != 0) {
        GOBJ_WORK(self)->stopFrames += 1;
    } else {
        GOBJ_WORK(self)->stopFrames = 0;
    }
    if (4 <= GOBJ_WORK(self)->slowFrames) {
        ((ActStatusWord *)&GOBJ_WORK(self)->stopFrames)->q |= 1ULL << 32;
    } else {
        ((ActStatusWord *)&GOBJ_WORK(self)->stopFrames)->q &= ~(1ULL << 32);
    }
    if (4 <= GOBJ_WORK(self)->stopFrames) {
        ((ActStatusWord *)&GOBJ_WORK(self)->stopFrames)->q |= 1ULL << 33;
    } else {
        ((ActStatusWord *)&GOBJ_WORK(self)->stopFrames)->q &= ~(1ULL << 33);
    }
    nomove = 0;
    if (((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags >> 10) & 1) {
        if (GOBJ_WORK(self)->speed < 4.0f) {
            nomove = 1;
        }
    }
    if (nomove != 0) {
        GOBJ_WORK(self)->noMoveFrames += 1;
    } else {
        GOBJ_WORK(self)->noMoveFrames = 0;
    }
    if (GOBJ_WORK(self)->noMoveFrames > (60 - systemStatus[0] * 10) / systemStatus[1] * 5) {
        ((ActStatusWord *)&GOBJ_WORK(self)->noMoveFrames)->q |= 1ULL << 32;
    } else {
        ((ActStatusWord *)&GOBJ_WORK(self)->noMoveFrames)->q &= ~(1ULL << 32);
    }
}

/* mail 188's function in the attack rows: the weapon is dropped (aug6
   has the same release as WithMailFunc_AttackRejectInQueen, commonact.c) */
inline void WithMailFunc_AttackReject(GObj *self) /* derived name */
{
    Act *s = GOBJ_ACT(self);

    if (s->weapon != 0) {
        ReleaseWeaponWithFumbleSequential(s->weapon);
        s->weapon = 0;
    }
}

inline int ACTNotNeedCameraOffset(GObj *self)
{
    Act *s;
    if (self != 0 && self == boyGObj) {
        s = GOBJ_ACT(self);
        if ((char *)s != 0) {
            return ((s->flags20.ll & (PAL_NTSC_AUG6(1ULL << 41, 1ULL << 24, 1ULL << 17))) != 0);
        }
    }
    return 0;
}

static void ACTGame_BeforeFunc_ntsc(GObj *self)
{
    Act *s = GOBJ_ACT(self);

    ACTGame_InnerVelocityUpdate(self);

    switch ((unsigned int)s->actMode) {
    case 1: {
        Act *p = GOBJ_ACT(self);
        p->attacker = 0;
        p->hit = 0;
        break;
    }
    }

    if (((int)(s->flags20.ll >> 23) & 1) == 0 &&
        ((((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags >> 31) & 1) == 0 ||
         GOBJ_SUB(self)->ctrl.reserveMoved != 0)) {
        GetRootPosition((char *)&s->camRootX, self);
    }

    s->flags20.ll &= ~(1ULL << 24);
    s->flags20.ll &= ~(1ULL << 23);

    ACTParaStatus_Clear(self);
    _ACTCharStatus_Clear(self);
    ACTLookTarget_Init(self);

    memset((char *)((char *)&s->wish0.ll + 4), 0, 0x10);
    memset((char *)((char *)&s->wish2.ll + 4), 0, 0x10);

    ACTGame_SetMotionPlaySpeedRatio_Clear(self);

    if (GOBJ_WORK(self)->downTimer > 0) {
        (GOBJ_WORK(self)->downTimer)--;
    }
    if (s->soundWait > 0) {
        (s->soundWait)--;
    }
    if (GOBJ_WORK(self)->turnTimer > 0) {
        (GOBJ_WORK(self)->turnTimer)--;
    }
    if (GOBJ_WORK(self)->turnTimer2 > 0) {
        (GOBJ_WORK(self)->turnTimer2)--;
    }
    if (GOBJ_WORK(self)->wishHoldTimer != 0) {
        (GOBJ_WORK(self)->wishHoldTimer)--;

        switch ((unsigned int)s->actMode) {
        case 2:
        case 3:
            break;

        default:
            GOBJ_WORK(self)->wishHoldTimer = 0;
            break;
        }
        if (scpBoyControlReadDisable != 0) {
            GOBJ_WORK(self)->wishHoldTimer = 0;
        }
    }

    if (((char *)girlGObj) != 0 && GOBJ_ACT(((char *)girlGObj))->actMode == 107 &&
        GOBJ_ACT(girlGObj)->carrier == self && ((int)(s->flags20.ll >> 10) & 1) == 0) {
        (GOBJ_WORK(self)->carryGirlFrames)++;
    } else {
        GOBJ_WORK(self)->carryGirlFrames = 0;
    }

    if (GOBJ_WORK(self)->leverTimer != 0) {
        (GOBJ_WORK(self)->leverTimer)--;
    }
    if (GOBJ_WORK(self)->noInterpTimer > 0) {
        (GOBJ_WORK(self)->noInterpTimer)--;
        SetDirectMotionProgramInterpInfo(self, 0x2C, 0.0f);
        SetDirectMotionProgramInterpInfo(self, 0, 0.0f);
        SetDirectMotionProgramInterpInfo(self, 1, 0.0f);
        SetDirectMotionProgramInterpInfo(self, 0x22, 0.0f);
        SetDirectMotionProgramInterpInfo(self, 0x23, 0.0f);
    }
    if (GOBJ_WORK(self)->sofaTimer != 0) {
        (GOBJ_WORK(self)->sofaTimer)--;
    }

    if (self == (girlGObj)) {
        ACTGame_GirlBeforeFunc(self);
    }

    if (((struct GObj *)self)->labelId == 3453) {
        if ((60 - systemStatus[0] * 10) / systemStatus[1] * 2 < s->frame) {
            if (!(((char *)girlGObj) != 0 && GOBJ_ACT(((char *)girlGObj))->actMode == 107 &&
                  GOBJ_ACT(girlGObj)->carrier == self)) {
                s->flags20.ll &= ~(1ULL << 17);
            }
        }
    }
}

static void ACTGame_BeforeFunc_jun26(GObj *self)
{
    Act *s = GOBJ_ACT(self);

    ACTGame_InnerVelocityUpdate(self);

    switch ((unsigned int)s->actMode) {
    case 1: {
        Act *p = GOBJ_ACT(self);
        p->attacker = 0;
        p->hit = 0;
        break;
    }
    }

    if ((((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags >> 31) & 1) == 0 ||
        GOBJ_SUB(self)->ctrl.reserveMoved != 0) {
        GetRootPosition((char *)&s->camRootX, self);
    }

    ACTParaStatus_Clear(self);
    _ACTCharStatus_Clear(self);
    ACTLookTarget_Init(self);

    memset((char *)((char *)&s->wish0.ll + 4), 0, PAL_JUN26(0x10, 12));
    memset((char *)((char *)&s->wish2.ll + 4), 0, PAL_JUN26(0x10, 12));

    ACTGame_SetMotionPlaySpeedRatio_Clear(self);

    if (GOBJ_WORK(self)->downTimer > 0) {
        (GOBJ_WORK(self)->downTimer)--;
    }
    if (s->soundWait > 0) {
        (s->soundWait)--;
    }

    if (self == (girlGObj)) {
        ACTGame_GirlBeforeFunc(self);
    }
}

static void ACTGame_BeforeFunc_aug6(GObj *self)
{
    Act *s = GOBJ_ACT(self);

    ACTGame_InnerVelocityUpdate(self);

    switch ((unsigned int)s->actMode) {
    case 1: {
        Act *p = GOBJ_ACT(self);
        p->attacker = 0;
        p->hit = 0;
        break;
    }
    }

    if (((int)(s->flags20.ll >> 16) & 1) == 0 &&
        ((((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags >> 31) & 1) == 0 ||
         GOBJ_SUB(self)->ctrl.reserveMoved != 0)) {
        GetRootPosition((char *)&s->camRootX, self);
    }

    s->flags20.ll &= ~(1ULL << 17);
    s->flags20.ll &= ~(1ULL << 16);

    ACTParaStatus_Clear(self);
    _ACTCharStatus_Clear(self);
    ACTLookTarget_Init(self);

    memset((char *)((char *)&s->wish0.ll + 4), 0, 0x10);
    memset((char *)((char *)&s->wish2.ll + 4), 0, 0x10);

    ACTGame_SetMotionPlaySpeedRatio_Clear(self);

    if (GOBJ_WORK(self)->downTimer > 0) {
        (GOBJ_WORK(self)->downTimer)--;
    }
    if (s->soundWait > 0) {
        (s->soundWait)--;
    }
    if (GOBJ_WORK(self)->turnTimer > 0) {
        (GOBJ_WORK(self)->turnTimer)--;
    }
    if (GOBJ_WORK(self)->turnTimer2 > 0) {
        (GOBJ_WORK(self)->turnTimer2)--;
    }
    if (GOBJ_WORK(self)->wishHoldTimer != 0) {
        (GOBJ_WORK(self)->wishHoldTimer)--;

        switch ((unsigned int)s->actMode) {
        case 2:
        case 3:
            break;

        default:
            GOBJ_WORK(self)->wishHoldTimer = 0;
            break;
        }
        if (scpBoyControlReadDisable != 0) {
            GOBJ_WORK(self)->wishHoldTimer = 0;
        }
    }

    if (((char *)girlGObj) != 0 && GOBJ_ACT(((char *)girlGObj))->actMode == 107 &&
        GOBJ_ACT(girlGObj)->carrier == self) {
        (GOBJ_WORK(self)->carryGirlFrames)++;
    } else {
        GOBJ_WORK(self)->carryGirlFrames = 0;
    }

    if (GOBJ_WORK(self)->leverTimer != 0) {
        (GOBJ_WORK(self)->leverTimer)--;
    }
    if (GOBJ_WORK(self)->noInterpTimer > 0) {
        (GOBJ_WORK(self)->noInterpTimer)--;
        SetDirectMotionProgramInterpInfo(self, 0x2C, 0.0f);
        SetDirectMotionProgramInterpInfo(self, 0, 0.0f);
        SetDirectMotionProgramInterpInfo(self, 1, 0.0f);
        SetDirectMotionProgramInterpInfo(self, 0x22, 0.0f);
        SetDirectMotionProgramInterpInfo(self, 0x23, 0.0f);
    }

    if (self == (girlGObj)) {
        ACTGame_GirlBeforeFunc(self);
    }
}

void ACTGame_BeforeFunc(GObj *self)
{
    if (ICO_JUN26) {
        ACTGame_BeforeFunc_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        ACTGame_BeforeFunc_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        ACTGame_BeforeFunc_aug6(self);
        return;
    }
    Act *s = GOBJ_ACT(self);

    ((ActWork *)s->work)->parallelInterp = 3.0f;
    ACTGame_InnerVelocityUpdate(self);

    switch ((unsigned int)s->actMode) {
    case 1:
    case 2:
    case 3: {
        Act *p = GOBJ_ACT(self);
        p->attacker = 0;
        p->hit = 0;
        break;
    }
    }

    if (((int)(s->flags20.ll >> 40) & 1) == 0 &&
        ((int)(&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags >= 0 ||
         GOBJ_SUB(self)->ctrl.reserveMoved != 0)) {
        GetRootPosition((char *)&s->camRootX, self);
    }

    s->flags20.ll &= ~(1ULL << 41);
    s->flags20.ll &= ~(1ULL << 40);

    ACTParaStatus_Clear(self);
    _ACTCharStatus_Clear(self);
    ACTLookTarget_Init(self);

    memset((char *)((char *)&s->wish0.ll + 4), 0, 0x10);
    memset((char *)((char *)&s->wish2.ll + 4), 0, 0x10);

    ACTGame_SetMotionPlaySpeedRatio_Clear(self);

    if (GOBJ_WORK(self)->downTimer > 0) {
        (GOBJ_WORK(self)->downTimer)--;
    }
    if (s->soundWait > 0) {
        (s->soundWait)--;
    }
    if (GOBJ_WORK(self)->turnTimer > 0) {
        (GOBJ_WORK(self)->turnTimer)--;
    }
    if (GOBJ_WORK(self)->turnTimer2 > 0) {
        (GOBJ_WORK(self)->turnTimer2)--;
    }
    if (GOBJ_WORK(self)->wishHoldTimer != 0) {
        (GOBJ_WORK(self)->wishHoldTimer)--;

        switch ((unsigned int)s->actMode) {
        case 2:
        case 3:
            break;

        default:
            GOBJ_WORK(self)->wishHoldTimer = 0;
            break;
        }
        if (scpBoyControlReadDisable != 0) {
            GOBJ_WORK(self)->wishHoldTimer = 0;
        }
    }

    if (((char *)girlGObj) != 0 && GOBJ_ACT(((char *)girlGObj))->actMode == 0x6F &&
        GOBJ_ACT(girlGObj)->carrier == self && ((int)(s->flags20.ll >> 21) & 1) == 0) {
        (GOBJ_WORK(self)->carryGirlFrames)++;
    } else {
        GOBJ_WORK(self)->carryGirlFrames = 0;
    }

    if (GOBJ_WORK(self)->leverTimer != 0) {
        (GOBJ_WORK(self)->leverTimer)--;
    }
    if (GOBJ_WORK(self)->noInterpTimer > 0) {
        (GOBJ_WORK(self)->noInterpTimer)--;
        SetDirectMotionProgramInterpInfo(self, 0x2C, 0.0f);
        SetDirectMotionProgramInterpInfo(self, 0, 0.0f);
        SetDirectMotionProgramInterpInfo(self, 1, 0.0f);
        SetDirectMotionProgramInterpInfo(self, 0x22, 0.0f);
        SetDirectMotionProgramInterpInfo(self, 0x23, 0.0f);
    }
    if (GOBJ_WORK(self)->sofaTimer != 0) {
        (GOBJ_WORK(self)->sofaTimer)--;
    }
    if (GOBJ_WORK(self)->sofaRestTimer != 0) {
        (GOBJ_WORK(self)->sofaRestTimer)--;
    }
    if (GOBJ_WORK(self)->timer3AC != 0) {
        (GOBJ_WORK(self)->timer3AC)--;
    }
    if (GOBJ_WORK(self)->turnMailWait != 0) {
        (GOBJ_WORK(self)->turnMailWait)--;
    }
    if (GOBJ_WORK(self)->jumpTimer != 0) {
        (GOBJ_WORK(self)->jumpTimer)--;
    }
    if (GOBJ_WORK(self)->mailB1Timer != 0) {
        (GOBJ_WORK(self)->mailB1Timer)--;
    }
    if (GOBJ_WORK(self)->ditchTimer != 0) {
        (GOBJ_WORK(self)->ditchTimer)--;
    }

    if ((int)(s->flags18.ll >> 36) & 1) {
        (GOBJ_WORK(self)->footIkFrames)++;
    } else {
        GOBJ_WORK(self)->footIkFrames = 0;
    }
    if ((int)(s->flags18.ll >> 37) & 1) {
        (GOBJ_WORK(self)->bit37Frames)++;
    } else {
        GOBJ_WORK(self)->bit37Frames = 0;
    }

    if ((((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags >> 14) & 1) ||
        actModeTbl[GOBJ_ACT(self)->actMode].bit14) {
        s->flags18.ll |= 1ULL << 38;
        if (self == boyGObj) {
            if (((char *)girlGObj) != 0) {
                GOBJ_WORK(((char *)girlGObj))->turnMailWait =
                    (60 - systemStatus[0] * 10) / systemStatus[1];
            }
        }
    }

    if ((int)(s->flags18.ll >> 38) & 1) {
        (GOBJ_WORK(self)->bit38Frames)++;
    } else {
        GOBJ_WORK(self)->bit38Frames = 0;
    }

    if (self == (girlGObj)) {
        ACTGame_GirlBeforeFunc(self);
    }

    if (((struct GObj *)self)->labelId == PAL_JPNKORCN(0xEAD, 3734)) {
        if ((60 - systemStatus[0] * 10) / systemStatus[1] * 2 < s->frame) {
            if (!(((char *)girlGObj) != 0 && GOBJ_ACT(((char *)girlGObj))->actMode == 0x6F &&
                  GOBJ_ACT(girlGObj)->carrier == self)) {
                s->flags20.ll &= ~(1ULL << 30);
            }
        }
    }
}

/* OR the 16 pending-request bytes into the live request bytes. */
static inline void actEnv_OrRequestBytes(unsigned char *dst, unsigned char *src) /* derived name */
{
    int i;

    for (i = PAL_JUN26(15, 11); i >= 0; i--, dst++, src++) {
        *dst |= *src;
    }
}

/* masks the 16-byte request-flag block in place */
static inline void andRequestFlags(char *d, char *m) /* derived name */
{
    int i;
    for (i = PAL_JUN26(15, 11); i >= 0; i--) {
        *d = *d & *m;
        d++;
        m++;
    }
}

static void FunctionAboutClingedStatus(GObj *self)
{
    int buf[4];
    Act *s;
    GObj *g;
    int clinged;
    int mode;
    int st;
    EnemyBattleWork *p;
    unsigned char cl;

    clinged = 0;
    mode = 0;
    s = GOBJ_ACT(self);
    g = isysGObjSearchFromObjKindID_begin(4);
    while (g != 0) {
        if (self == actEnemy_GetClingTarget(g)) {
            clinged = 1;
            break;
        }
        g = isysGObjSearchFromObjKindID_next(g);
    }
    if (clinged != 0) {
        _ACTCharStatus_Set(self, PAL_NTSCAUG6_JUN26(30, 27, 25), 0.0f, 0);
        _ACTParaStatus_Set(self, PAL_NTSCAUG6JUN26(37, 0));
    }
    cl = _ACTCharStatus_Check(self, PAL_NTSCAUG6_JUN26(30, 27, 25));
    if (cl != 0) {
        st = s->actMode;
        if (st != 0) {
            if ((unsigned int)st >= 4) {
                if (st == 15) {
                    mode = 1;
                    if (s->modeFrame == 0) {
                        p = GOBJ_ACT(self)->enemy;
                        p->clingedFrames += 1;
                        if (GOBJ_ACT(self)->enemy->clingedFrames >= 5) {
                            mode = 2;
                        }
                    }
                }
            } else {
                mode = 1;
            }
        }
    } else {
        GOBJ_ACT(self)->enemy->clingedFrames = 0;
    }
    switch (mode) {
    case 0:
        break;
    case 1:
        memset(buf, 0, PAL_JUN26(0x10, 12));
        buf[0] |= PAL_NTSCAUG6JUN26(0x1000, 0x400);
        andRequestFlags((char *)((char *)&s->wish2.ll + 4), (char *)buf);
        ACTGame_SetMotionPlaySpeedRatio_Reserve(self, 1.0f / ((float)clinged * 0.25f + 1.0f), 6);
        break;
    case 2:
        g = isysGObjSearchFromObjKindID_begin(4);
        while (g != 0) {
            if (self == actEnemy_GetClingTarget(g)) {
                iosOmSendMail(g, PAL_NTSC_AUG6_JUN26(0xD6, 198, 195, 191), self);
                break;
            }
            g = isysGObjSearchFromObjKindID_next(g);
        }
        GOBJ_ACT(self)->enemy->clingedFrames = 0;
        break;
    }
}

static void ACTEnvGetTest_ntsc(GObj *self, void *dir)
{
    ActEnv old;
    Act *s = GOBJ_ACT(self);

    s->flags18.ll &= ~(1ULL << 50);
    s->flags18.ll &= ~(1ULL << 51);
    s->flags18.ll &= ~(1ULL << 52);
    s->flags20.ll &= ~(1ULL << 8);
    s->flags20.ll &= ~(1ULL << 22);

    switch (s->actMode) {
    case 38:
    case 104:
        s->env.motOriReq = *(MotOriReq *)&GOBJ_SUB(self)->root.wall;
        break;

    default:
        old = s->env;
        memset(&s->env, 0, sizeof(ActEnv));
        s->env.turnDir = old.turnDir;
        s->env.motOriReq = old.motOriReq;
        s->env.supportReq = old.supportReq;
        s->env.cliffContact = old.cliffContact;
        ACTGetEnvironment(self, dir, test_CURRENTORIENT(self),
                          (EnvFlag *)((char *)&s->wish0.ll + 4), &s->env);
        break;

    case 10:
    case 12:
    case 14:
    case 26:
    case 36:
    case 45:
    case 46:
    case 47:
    case 48:
    case 51:
    case 52:
    case 53:
    case 54:
    case 63:
    case 64:
    case 65:
    case 66:
    case 75:
    case 76:
    case 77:
    case 78:
    case 79:
    case 85:
    case 101:
    case 102:
    case 103:
        break;
    }

    ACTSetEnvAllmighty(self);
    ACTGetWish_FromPad(self, dir);

    if (ACTGame_CheckPriInputFrame(self)) {
        actEnv_OrRequestBytes((unsigned char *)((char *)&s->wish4.ll + 4),
                              (unsigned char *)((char *)&s->wish2.ll + 4));
    } else {
        memset((char *)((char *)&s->wish4.ll + 4), 0, 0x10);
    }

    if ((int)(s->wish4.ll >> 38) & 1) {
        s->wish2.ll |= 1ULL << 38;
    }
    if ((int)(s->wish4.ll >> 42) & 1) {
        s->wish2.ll |= 1ULL << 42;
    }
    if ((int)(s->wish4.ll >> 43) & 1) {
        s->wish2.ll |= 1ULL << 43;
    }
    if ((int)(s->wish4.ll >> 47) & 1) {
        s->wish2.ll |= 1ULL << 47;
    }
    if ((int)(s->wish4.ll >> 48) & 1) {
        s->wish2.ll |= 1ULL << 48;
    }
    if ((int)(s->wish4.ll >> 49) & 1) {
        s->wish2.ll |= 1ULL << 49;
    }
    if ((int)(s->wish4.ll >> 50) & 1) {
        s->wish2.ll |= 1ULL << 50;
    }
    FunctionAboutClingedStatus(self);
}

static void ACTEnvGetTest_jun26(GObj *self, void *dir)
{
    ActEnv old;
    Act *s = GOBJ_ACT(self);

    s->flags18.ll &= ~(1ULL << 48);
    s->flags18.ll &= ~(1ULL << 49);
    s->flags18.ll &= ~(1ULL << 50);
    s->flags20.ll &= ~(1ULL << 3);

    switch (s->actMode) {
    case 38:
    case 103:
        s->env.motOriReq = *(MotOriReq *)&GOBJ_SUB(self)->root.wall;
        break;

    default:
        old = s->env;
        memset(&s->env, 0, sizeof(ActEnv));
        s->env.turnDir = old.turnDir;
        s->env.motOriReq = old.motOriReq;
        s->env.supportReq = old.supportReq;
        s->env.cliffContact = old.cliffContact;
        ACTGetEnvironment(self, dir, test_CURRENTORIENT(self),
                          (EnvFlag *)((char *)&s->wish0.ll + 4), &s->env);
        break;

    case 10:
    case 12:
    case 14:
    case 26:
    case 36:
    case 45:
    case 46:
    case 47:
    case 48:
    case 51:
    case 52:
    case 53:
    case 54:
    case 63:
    case 64:
    case 65:
    case 66:
    case 74:
    case 75:
    case 76:
    case 77:
    case 78:
    case 84:
    case 100:
    case 101:
    case 102:
        break;
    }

    ACTSetEnvAllmighty(self);
    ACTGetWish_FromPad(self, dir);

    if (ACTGame_CheckPriInputFrame(self)) {
        actEnv_OrRequestBytes((unsigned char *)((char *)&s->wish4.ll + 4),
                              (unsigned char *)((char *)&s->wish2.ll + 4));
    } else {
        memset((char *)((char *)&s->wish4.ll + 4), 0, PAL_JUN26(0x10, 12));
    }

    if ((int)(s->wish3.ll >> 38) & 1) {
        s->wish2.ll |= 1ULL << 6;
    }
    if ((int)(s->wish3.ll >> 42) & 1) {
        s->wish2.ll |= 1ULL << 10;
    }
    if ((int)(s->wish3.ll >> 43) & 1) {
        s->wish2.ll |= 1ULL << 11;
    }
    if ((int)(s->wish3.ll >> 47) & 1) {
        s->wish2.ll |= 1ULL << 15;
    }
    if ((int)(s->wish3.ll >> 48) & 1) {
        s->wish2.ll |= 1ULL << 16;
    }
    if ((int)(s->wish3.ll >> 49) & 1) {
        s->wish2.ll |= 1ULL << 17;
    }
    if ((int)(s->wish3.ll >> 50) & 1) {
        s->wish2.ll |= 1ULL << 18;
    }
    FunctionAboutClingedStatus(self);
}

static void ACTEnvGetTest_aug6(GObj *self, void *dir)
{
    ActEnv old;
    Act *s = GOBJ_ACT(self);

    s->flags18.ll &= ~(1ULL << 49);
    s->flags18.ll &= ~(1ULL << 50);
    s->flags18.ll &= ~(1ULL << 51);
    s->flags20.ll &= ~(1ULL << 5);
    s->flags20.ll &= ~(1ULL << 15);

    switch (s->actMode) {
    case 38:
    case 104:
        s->env.motOriReq = *(MotOriReq *)&GOBJ_SUB(self)->root.wall;
        break;

    default:
        old = s->env;
        memset(&s->env, 0, sizeof(ActEnv));
        s->env.turnDir = old.turnDir;
        s->env.motOriReq = old.motOriReq;
        s->env.supportReq = old.supportReq;
        s->env.cliffContact = old.cliffContact;
        ACTGetEnvironment(self, dir, test_CURRENTORIENT(self),
                          (EnvFlag *)((char *)&s->wish0.ll + 4), &s->env);
        break;

    case 10:
    case 12:
    case 14:
    case 26:
    case 36:
    case 45:
    case 46:
    case 47:
    case 48:
    case 51:
    case 52:
    case 53:
    case 54:
    case 63:
    case 64:
    case 65:
    case 66:
    case 75:
    case 76:
    case 77:
    case 78:
    case 79:
    case 85:
    case 101:
    case 102:
    case 103:
        break;
    }

    ACTSetEnvAllmighty(self);
    ACTGetWish_FromPad(self, dir);

    if (ACTGame_CheckPriInputFrame(self)) {
        actEnv_OrRequestBytes((unsigned char *)((char *)&s->wish4.ll + 4),
                              (unsigned char *)((char *)&s->wish2.ll + 4));
    } else {
        memset((char *)((char *)&s->wish4.ll + 4), 0, 0x10);
    }

    if ((int)(s->wish4.ll >> 38) & 1) {
        s->wish2.ll |= 1ULL << 38;
    }
    if ((int)(s->wish4.ll >> 42) & 1) {
        s->wish2.ll |= 1ULL << 42;
    }
    if ((int)(s->wish4.ll >> 43) & 1) {
        s->wish2.ll |= 1ULL << 43;
    }
    if ((int)(s->wish4.ll >> 47) & 1) {
        s->wish2.ll |= 1ULL << 47;
    }
    if ((int)(s->wish4.ll >> 48) & 1) {
        s->wish2.ll |= 1ULL << 48;
    }
    if ((int)(s->wish4.ll >> 49) & 1) {
        s->wish2.ll |= 1ULL << 49;
    }
    if ((int)(s->wish4.ll >> 50) & 1) {
        s->wish2.ll |= 1ULL << 50;
    }
    FunctionAboutClingedStatus(self);
}

static void ACTEnvGetTest(GObj *self, void *dir)
{
    if (ICO_JUN26) {
        ACTEnvGetTest_jun26(self, dir);
        return;
    }
    if (ICO_NTSC) {
        ACTEnvGetTest_ntsc(self, dir);
        return;
    }
    if (ICO_AUG6) {
        ACTEnvGetTest_aug6(self, dir);
        return;
    }
    ActEnv old;
    Act *s = GOBJ_ACT(self);

    s->flags18.ll &= ~(1ULL << 59);
    s->flags18.ll &= ~(1ULL << 60);
    s->flags18.ll &= ~(1ULL << 61);
    s->flags20.ll &= ~(1ULL << 19);
    s->flags20.ll &= ~(1ULL << 38);
    s->flags20.ll &= ~(1ULL << 39);

    switch (s->actMode) {
    case 38:
    case 107:
        s->env.motOriReq = *(MotOriReq *)&GOBJ_SUB(self)->root.wall;
        break;

    default:
        old = s->env;
        memset(&s->env, 0, sizeof(ActEnv));
        s->env.turnDir = old.turnDir;
        s->env.motOriReq = old.motOriReq;
        s->env.supportReq = old.supportReq;
        s->env.cliffContact = old.cliffContact;
        ACTGetEnvironment(self, dir, test_CURRENTORIENT(self),
                          (EnvFlag *)((char *)&s->wish0.ll + 4), &s->env);
        break;

    case 10:
    case 12:
    case 14:
    case 26:
    case 36:
    case 45:
    case 46:
    case 47:
    case 48:
    case 51:
    case 52:
    case 53:
    case 54:
    case 63:
    case 64:
    case 65:
    case 66:
    case 78:
    case 79:
    case 80:
    case 81:
    case 82:
    case 88:
    case 104:
    case 105:
    case 106:
        break;
    }

    ACTSetEnvAllmighty(self);
    ACTGetWish_FromPad(self, dir);

    if (ACTGame_CheckPriInputFrame(self)) {
        actEnv_OrRequestBytes((unsigned char *)((char *)&s->wish4.ll + 4),
                              (unsigned char *)((char *)&s->wish2.ll + 4));
    } else {
        memset((char *)((char *)&s->wish4.ll + 4), 0, 0x10);
    }

    if ((int)(s->wish4.ll >> 39) & 1) {
        s->wish2.ll |= 1ULL << 39;
    }
    if ((int)(s->wish4.ll >> 44) & 1) {
        s->wish2.ll |= 1ULL << 44;
    }
    if ((int)(s->wish4.ll >> 45) & 1) {
        s->wish2.ll |= 1ULL << 45;
    }
    if ((int)(s->wish4.ll >> 50) & 1) {
        s->wish2.ll |= 1ULL << 50;
    }
    if ((int)(s->wish4.ll >> 51) & 1) {
        s->wish2.ll |= 1ULL << 51;
    }
    if ((int)(s->wish4.ll >> 52) & 1) {
        s->wish2.ll |= 1ULL << 52;
    }
    if ((int)(s->wish4.ll >> 53) & 1) {
        s->wish2.ll |= 1ULL << 53;
    }
    FunctionAboutClingedStatus(self);
}

static void ActOrientTest_ntsc(GObj *self)
{
    float v0[4];
    ClipWork w1;
    float p1[4];
    float sk1[4];
    float sk2[4];
    float d1[4];
    ClipWork w2;
    float p2[4];
    float d2[4];
    float c1[4];
    ClipWork w3;
    float sk3[4];
    float sk4[4];
    float d3[4];
    float ow[4];
    Act *s = GOBJ_ACT(self);
    float *vel;
    int hitA;
    int hitB;
    int near;
    int i;

    if (ORBIT(ORQ((char *)s, 0), 38) && ORBIT(ORM((char *)s, 0), 38)) {
        ACTSendMailCorrect(self, 176);
    }
    if (ORBIT(ORQ((char *)s, 0), 39) && ORBIT(ORM((char *)s, 0), 39)) {
        ACTSendMailCorrect(self, 178);
    }
    if (ORBIT(ORQ((char *)s, 0), 40) && ORBIT(ORM((char *)s, 0), 40)) {
        ACTSendMailCorrect(self, 179);
    }
    if (ORBIT(ORQ((char *)s, 0), 41) && ORBIT(ORM((char *)s, 0), 41)) {
        ACTSendMailCorrect(self, 180);
    }
    if (ORBIT(ORQ((char *)s, 0), 42) && ORBIT(ORM((char *)s, 0), 42)) {
        if (((struct GObj *)self)->kind == 4) {
            if (rand() & 1) {
                ACTSendMailCorrect(self, 189);
            } else {
                ACTSendMailCorrect(self, 191);
            }
        } else {
            ACTSendMailCorrect(self, 189);
        }
    }
    if (ORBIT(ORQ((char *)s, 0), 43) && ORBIT(ORM((char *)s, 0), 43)) {
        ACTSendMailCorrect(self, 190);
    }
    if (ORBIT(ORQ((char *)s, 0), 48) && ORBIT(ORM((char *)s, 0), 48)) {
        ACTSendMailCorrect(self, 263);
    }
    if (self != boyGObj) {
        if (ORBIT(ORQ((char *)s, 0), 47) && ORBIT(ORM((char *)s, 0), 47)) {
            ACTSendMailCorrect(self, 262);
        }
        if (ORBIT(ORQ((char *)s, 0), 49) && ORBIT(ORM((char *)s, 0), 49)) {
            ACTSendMailCorrect(self, 264);
        }
        if (ORBIT(ORQ((char *)s, 0), 50) && ORBIT(ORM((char *)s, 0), 50)) {
            ACTSendMailCorrect(self, 265);
        }
    }
    if (ORBIT(ORQ((char *)s, 0), 52) && ORBIT(ORM((char *)s, 0), 52)) {
        ACTSendMailCorrect(self, 187);
    }
    if (ORBIT(ORQ((char *)s, 0), 51) && ORBIT(ORM((char *)s, 0), 51)) {
        if (ACTGame_NoWeapon(self)) {
            ACTSendMailCorrect(self, 186);
        } else {
            ACTSendMailCorrect(self, 185);
        }
    }
    if (ORBIT(ORQ((char *)s, 0), 53) && ORBIT(ORM((char *)s, 0), 53)) {
        ACTSendMailCorrect(self, 320);
    }
    if (ORBIT(ORQ((char *)s, 0), 54) && ORBIT(ORM((char *)s, 0), 54)) {
        ACTSendMailCorrect(self, 350);
    }
    if (ORBIT(ORQ((char *)s, 0), 57) && ORBIT(ORM((char *)s, 0), 57)) {
        ACTSendMailCorrect(self, 193);
    }
    if (ORBIT(ORQ((char *)s, 0), 58) && ORBIT(ORM((char *)s, 0), 58)) {
        ACTSendMailCorrect(self, 194);
    }
    if (ORBIT(ORQ((char *)s, 0), 59) && ORBIT(ORM((char *)s, 0), 59)) {
        ACTSendMailCorrect(self, 196);
    }
    if (ORBIT(ORQ((char *)s, 0), 60) && ORBIT(ORM((char *)s, 0), 60)) {
        ACTSendMailCorrect(self, 197);
    }
    if (ORBIT(ORQ((char *)s, 0), 61) && ORBIT(ORM((char *)s, 0), 61)) {
        ActSendMail_WithAdditionalData(
            self, 247, self, (char *)&GOBJ_WORK(self)->effRec[0]); /* PC port: the host record's */
    }
    if (ORBIT(ORQ((char *)s, 0), 62) && ORBIT(ORM((char *)s, 0), 62)) {
        ActSendMail_WithAdditionalData(
            self, 248, self, (char *)&GOBJ_WORK(self)->effRec[1]); /* PC port: the host record's */
    }
    if (ORBIT(ORQ((char *)s, 0), 63) && ORBIT(ORM((char *)s, 0), 63)) {
        ActSendMail_WithAdditionalData(
            self, 249, self, (char *)&GOBJ_WORK(self)->effRec[2]); /* PC port: the host record's */
    }
    if (ORBIT(ORQ((char *)s, 0), 55) && ORBIT(ORM((char *)s, 0), 55)) {
        ACTSendMailCorrect(self, 161);
        ACTSendMailCorrect(self, 160);
    }
    if (ORBIT(ORQ((char *)s, 0), 56) && ORBIT(ORM((char *)s, 0), 56)) {
        ACTSendMailCorrect(self, 154);
    }
    if (ORBIT(ORQ((char *)s, 1), 4) && ORBIT(ORM((char *)s, 1), 4)) {
        ACTSendMailCorrect(self, 200);
        debug_StdPrintfDummy("shoal mail\n");
    } else if (s->actMode == 43) {
        ACTSendMailCorrect(self, 201);
    }
    if (ORBIT(ORQ((char *)s, 1), 5) && ORBIT(ORM((char *)s, 1), 5)) {
        ACTSendMailCorrect(self, 202);
    }
    if (ORBIT(ORQ((char *)s, 1), 6) && ORBIT(ORM((char *)s, 1), 6)) {
        ACTSendMailCorrect(self, 203);
    }
    if (ORBIT(ORQ((char *)s, 1), 7) && ORBIT(ORM((char *)s, 1), 7)) {
        ACTSendMailCorrect(self, 204);
    }
    if (ORBIT(ORQ((char *)s, 1), 8) && ORBIT(ORM((char *)s, 1), 8)) {
        ACTSendMailCorrect(self, 207);
    }
    if (ORBIT(ORQ((char *)s, 1), 9) && ORBIT(ORM((char *)s, 1), 9)) {
        ACTSendMailCorrect(self, 208);
    }
    if (ORBIT(ORQ((char *)s, 1), 10) && ORBIT(ORM((char *)s, 1), 10)) {
        ACTSendMailCorrect(self, 209);
    }
    if (ORBIT(ORQ((char *)s, 1), 29) && ORBIT(ORM((char *)s, 1), 29)) {
        ACTSendMailCorrect(self, 114);
    }
    if (ORBIT(ORQ((char *)s, 1), 31) && ORBIT(ORM((char *)s, 1), 31)) {
        ACTSendMailCorrect(self, 115);
    }
    if (ORBIT(ORQ((char *)s, 1), 33) && ORBIT(ORM((char *)s, 1), 33)) {
        ACTSendMailCorrect(self, 122);
    }
    if (ORBIT(ORQ((char *)s, 1), 32) && ORBIT(ORM((char *)s, 1), 32)) {
        ACTSendMailCorrect(self, 119);
    }
    if (ORBIT(ORQ((char *)s, 1), 34) && ORBIT(ORM((char *)s, 1), 34)) {
        ACTSendMailCorrect(self, 123);
    }
    if (ORBIT(ORQ((char *)s, 1), 35) && ORBIT(ORM((char *)s, 1), 35)) {
        ACTSendMailCorrect(self, 124);
    }
    if (ORBIT(ORQ((char *)s, 1), 36) && ORBIT(ORM((char *)s, 1), 36)) {
        ACTSendMailCorrect(self, 116);
    }
    if (ORBIT(ORQ((char *)s, 1), 37) && ORBIT(ORM((char *)s, 1), 37)) {
        ACTSendMailCorrect(self, 117);
        ACTSendMailCorrect(self, 118);
    }
    if (ORBIT(ORQ((char *)s, 1), 26) && ORBIT(ORM((char *)s, 1), 26)) {
        ACTSendMailCorrect(self, 111);
    }
    if (ORBIT(ORQ((char *)s, 1), 27) && ORBIT(ORM((char *)s, 1), 27)) {
        ACTSendMailCorrect(self, 112);
    }
    if (ORBIT(ORQ((char *)s, 1), 28) && ORBIT(ORM((char *)s, 1), 28)) {
        ACTSendMailCorrect(self, 113);
    }
    if (ORBIT(ORQ((char *)s, 1), 0) && ORBIT(ORM((char *)s, 1), 0)) {
        if (self == (girlGObj)) {
            /* PC port: with Players 2 outside GIRLREADYMOVE too, as the
               European ActOrientTest does */
            if (girlControlMode != 0 || GOBJ_ACT(self)->actMode == 113) {
                ACTSendMailCorrect(self, 107);
            }
        } else {
            ACTSendMailCorrect(self, 107);
        }
    }
    if (ORBIT(ORQ((char *)s, 1), 1) && ORBIT(ORM((char *)s, 1), 1)) {
        ACTSendMailCorrect(self, 305);
    }
    if (ORBIT(ORQ((char *)s, 1), 2) && ORBIT(ORM((char *)s, 1), 2)) {
        ACTSendMailCorrect(self, 306);
    }
    if (ORBIT(ORQ((char *)s, 1), 11) && ORBIT(ORM((char *)s, 1), 11)) {
        ACTSendMailCorrect(self, 298);
    }
    if (ORBIT(ORQ((char *)s, 1), 12) && ORBIT(ORM((char *)s, 1), 12)) {
        ACTSendMailCorrect(self, 299);
    }
    if (ORBIT(ORQ((char *)s, 1), 13) && ORBIT(ORM((char *)s, 1), 13)) {
        ACTSendMailCorrect(self, 300);
    }
    if (ORBIT(ORQ((char *)s, 1), 15) && ORBIT(ORM((char *)s, 1), 15)) {
        ACTSendMailCorrect(self, 301);
    }
    if (ORBIT(ORQ((char *)s, 1), 16) && ORBIT(ORM((char *)s, 1), 16)) {
        ACTSendMailCorrect(self, 301);
    }
    if (ORBIT(ORQ((char *)s, 1), 20) && ORBIT(ORM((char *)s, 1), 20)) {
        ACTSendMailCorrect(self, 298);
    }
    if (ORBIT(ORQ((char *)s, 1), 21) && ORBIT(ORM((char *)s, 1), 21)) {
        ACTSendMailCorrect(self, 299);
    }
    if (ORBIT(ORQ((char *)s, 1), 22) && ORBIT(ORM((char *)s, 1), 22)) {
        ACTSendMailCorrect(self, 300);
    }
    if (ORBIT(ORQ((char *)s, 1), 47) && ORBIT(ORM((char *)s, 1), 47)) {
        ACTSendMailCorrect(self, 42);
    }
    if (ORBIT(ORQ((char *)s, 1), 48) && ORBIT(ORM((char *)s, 1), 48)) {
        ACTSendMailCorrect(self, 41);
    }
    if (ORBIT(ORQ((char *)s, 1), 49) && ORBIT(ORM((char *)s, 1), 49)) {
        ACTSendMailCorrect(self, 127);
    }
    if (ORBIT(ORQ((char *)s, 1), 50) && ORBIT(ORM((char *)s, 1), 50)) {
        ACTSendMailCorrect(self, 128);
    }
    if (ORBIT(ORQ((char *)s, 1), 55) && ORBIT(ORM((char *)s, 1), 55)) {
        ACTSendMailCorrect(self, 280);
    }
    if (ORBIT(ORQ((char *)s, 1), 56) && ORBIT(ORM((char *)s, 1), 56)) {
        ACTSendMailCorrect(self, 282);
    }
    if ((ORBIT(ORQ((char *)s, 1), 57) && ORBIT(ORM((char *)s, 1), 57)) ||
        (ORBIT(ORQ((char *)s, 1), 58) && ORBIT(ORM((char *)s, 1), 58))) {
        ACTSendMailCorrect(self, 278);
    }
    if (ORBIT(ORQ((char *)s, 1), 45) && ORBIT(ORM((char *)s, 1), 45)) {
        ACTSendMailCorrect(self, 284);
    }
    if (ORBIT(ORQ((char *)s, 1), 46) && ORBIT(ORM((char *)s, 1), 46)) {
        ACTSendMailCorrect(self, 136);
    }
    if (ORBIT(ORQ((char *)s, 1), 38) && ORBIT(ORM((char *)s, 1), 38)) {
        ACTSendMailCorrect(self, 278);
    }
    if (ORBIT(ORQ((char *)s, 1), 39) && ORBIT(ORM((char *)s, 1), 39)) {
        ACTSendMailCorrect(self, 283);
    }
    if (ORBIT(ORQ((char *)s, 1), 40) && ORBIT(ORM((char *)s, 1), 40)) {
        ACTSendMailCorrect(self, 289);
    }
    if (ORBIT(ORQ((char *)s, 1), 41) && ORBIT(ORM((char *)s, 1), 41)) {
        ACTSendMailCorrect(self, 290);
    }
    if (ORBIT(ORQ((char *)s, 1), 42) && ORBIT(ORM((char *)s, 1), 42)) {
        ACTSendMailCorrect(self, 291);
    }
    if (ORBIT(ORQ((char *)s, 1), 43) && ORBIT(ORM((char *)s, 1), 43)) {
        ACTSendMailCorrect(self, 292);
    }
    if (ORBIT(ORQ((char *)s, 1), 44) && ORBIT(ORM((char *)s, 1), 44)) {
        ACTSendMailCorrect(self, 293);
    }
    if (ORBIT(ORQ((char *)s, 2), 2) && ORBIT(ORM((char *)s, 2), 2)) {
        ACTSendMailCorrect(self, 83);
    }
    if (ORBIT(ORQ((char *)s, 2), 3) && ORBIT(ORM((char *)s, 2), 3)) {
        ACTSendMailCorrect(self, 84);
    }
    if (ORBIT(ORQ((char *)s, 2), 4) && ORBIT(ORM((char *)s, 2), 4)) {
        (GOBJ_WORK(self)->orientFrames)++;
    } else {
        GOBJ_WORK(self)->orientFrames = 0;
    }
    if (ORBIT(ORQ((char *)s, 1), 63) && ORBIT(ORM((char *)s, 1), 63)) {
        ACTSendMailCorrect(self, 68);
        s->orientMot = 96;
    }
    if (ORBIT(ORQ((char *)s, 2), 0) && ORBIT(ORM((char *)s, 2), 0)) {
        ACTSendMailCorrect(self, 68);
        s->orientMot = 98;
    }
    if (ORBIT(ORQ((char *)s, 2), 1) && ORBIT(ORM((char *)s, 2), 1)) {
        ACTSendMailCorrect(self, 68);
        s->orientMot = 100;
    }
    if (ORBIT(ORQ((char *)s, 1), 17) && ORBIT(ORM((char *)s, 1), 17)) {
        ACTSendMailCorrect(self, 302);
    }
    if (ORBIT(ORQ((char *)s, 1), 18) && ORBIT(ORM((char *)s, 1), 18)) {
        ACTSendMailCorrect(self, 303);
    }
    if (ORBIT(ORQ((char *)s, 1), 19) && ORBIT(ORM((char *)s, 1), 19)) {
        ACTSendMailCorrect(self, 304);
    }
    if (ORBIT(ORQ((char *)s, 1), 54) && ORBIT(ORM((char *)s, 1), 54)) {
        ACTSendMailCorrect(self, 153);
    }
    if (ORBIT(ORQ((char *)s, 2), 5) && ORBIT(ORM((char *)s, 2), 5)) {
        ACTSendMailCorrect(self, 363);
    }
    if (ORBIT(ORQ((char *)s, 2), 6) && ORBIT(ORM((char *)s, 2), 6)) {
        vel = s->env.cliffOrient;
        sceVu0ScaleVector(v0, vel, -sceVu0InnerProduct((char *)GOBJ_SUB(self)->root.move, vel));
        sceVu0AddVector((char *)GOBJ_SUB(self)->root.move, (char *)GOBJ_SUB(self)->root.move, v0);
        SetRootPosition(self, s->env.cliffBackPos);
    }
    if (GOBJ_ACT(self)->enemy->stoneLevel > 0) {
        ACTSendMailCorrect(self, 104);
    }
    if (((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags2 >> 2) & 1) {
        if (GetMotionFrameFlag1(self)) {
            memset(&w1, 0, sizeof(w1));
            GetSkeltonPosition(sk1, self, 51);
            GetSkeltonPosition(sk2, self, 47);
            sceVu0AddVector(p1, sk1, sk2);
            sceVu0ScaleVector(p1, p1, 0.5f);
            p1[1] = p1[1] + 50.0f;
            sceVu0ScaleVector(d1, test_CURRENTORIENT(self), 50.0f);
            sceVu0AddVector(w1.pt[0], p1, d1);
            sceVu0ScaleVector(d1, test_CURRENTORIENT(self), -50.0f);
            sceVu0AddVector(w1.pt[1], p1, d1);
            w1.radius = 0.0f;
            ClipWall(&w1);
            if (CompareAttribute(w1.attr, 0x2000)) {
                ACTSendMailCorrect(self, 307);
            }
            if (CompareAttribute(w1.attr, 0x20000)) {
                if (s->addData != 0) {
                    if (GetMotionFrameFlag1(self)) {
                        char *ext;
                        ((MotOriTarget *)s->addData)->wall = w1.wall;
                        ext = (char *)s->addData;
                        ActSendMail_WithAdditionalData(self, 281, self, ext);
                    }
                }
            }
        }
    }
    if (((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags2 >> 3) & 1) {
        memset(&w2, 0, sizeof(w2));
        near = 0;
        p2[0] = test_CURRENTROOT(self)[0];
        p2[1] = test_CURRENTROOT(self)[1];
        p2[2] = test_CURRENTROOT(self)[2];
        sceVu0ScaleVector(d2, test_CURRENTORIENT(self), -50.0f);
        sceVu0AddVector(w2.pt[0], p2, d2);
        sceVu0ScaleVector(d2, test_CURRENTORIENT(self), 50.0f);
        sceVu0AddVector(w2.pt[1], p2, d2);
        hitA = 0;
        w2.radius = 0.0f;
        ClipWall(&w2);
        hitB = 0;
        if (CompareAttribute(w2.attr, 0x400)) {
            hitA = 1;
        }
        if (CompareAttribute(w2.attr, 0xC000)) {
            hitB = 1;
        }
        if (hitA || hitB) {
            GetCollisCenterPositionSimple(c1, w2.wall.o.obj, w2.wall.elem);
            if (_DistxzSqGV(c1, test_CURRENTROOT(self)) < 400.0f) {
                near = 1;
            }
        }
        if (hitA && near) {
            ACTSendMailCorrect(self, 131);
        }
        if (hitB && near) {
            ACTSendMailCorrect(self, 291);
        }
    }
    for (i = 0; i < 2; i++) {
        if (((int)(s->flags20.ll >> 25) & 1) == 0) {
            continue;
        }
        memset(&w3, 0, sizeof(w3));
        GetSkeltonPosition(sk3, self, 51);
        GetSkeltonPosition(sk4, self, 47);
        sceVu0AddVector(c1, sk3, sk4);
        sceVu0ScaleVector(c1, c1, 0.5f);
        c1[1] = c1[1] + 50.0f;
        sceVu0ScaleVector(d3, test_CURRENTORIENT(self), 50.0f);
        _ApplyRyGV(d3, -1.5707964f);
        sceVu0AddVector(w3.pt[0], c1, d3);
        sceVu0ScaleVector(d3, test_CURRENTORIENT(self), 50.0f);
        _ApplyRyGV(d3, 1.5707964f);
        sceVu0AddVector(w3.pt[1], c1, d3);
        if (i == 1) {
            SwapGV(w3.pt[0], w3.pt[1]);
        }
        w3.radius = 0.0f;
        ClipWall(&w3);
        if (w3.wall.elem != 0) {
            GetOrientOfWall(ow, w3.wall.elem, &w3.wall.o);
            SetMotionDirection(self, ow);
            s->flags20.ll &= ~(1ULL << 25);
            return;
        }
    }
}

static void ActOrientTest_jun26(GObj *self)
{
    float v0[4];
    ClipWork w1;
    float p1[4];
    float sk1[4];
    float sk2[4];
    float d1[4];
    ClipWork w2;
    float p2[4];
    float d2[4];
    float c1[4];
    Act *s = GOBJ_ACT(self);
    float *vel;
    int hitA;
    int hitB;
    int near;

    if (ORBIT(ORQ((char *)s, 0), 38) && ORBIT(ORM((char *)s, 0), 6)) {
        ACTSendMailCorrect(self, 171);
    }
    if (ORBIT(ORQ((char *)s, 0), 39) && ORBIT(ORM((char *)s, 0), 7)) {
        ACTSendMailCorrect(self, 172);
    }
    if (ORBIT(ORQ((char *)s, 0), 40) && ORBIT(ORM((char *)s, 0), 8)) {
        ACTSendMailCorrect(self, 173);
    }
    if (ORBIT(ORQ((char *)s, 0), 41) && ORBIT(ORM((char *)s, 0), 9)) {
        ACTSendMailCorrect(self, 174);
    }
    if (ORBIT(ORQ((char *)s, 0), 42) && ORBIT(ORM((char *)s, 0), 10)) {
        if (((struct GObj *)self)->kind == 4) {
            if (rand() & 1) {
                ACTSendMailCorrect(self, 183);
            } else {
                ACTSendMailCorrect(self, 185);
            }
        } else {
            ACTSendMailCorrect(self, 183);
        }
    }
    if (ORBIT(ORQ((char *)s, 0), 43) && ORBIT(ORM((char *)s, 0), 11)) {
        ACTSendMailCorrect(self, 184);
    }
    if (ORBIT(ORQ((char *)s, 0), 48) && ORBIT(ORM((char *)s, 0), 16)) {
        ACTSendMailCorrect(self, 253);
    }
    if (self != boyGObj) {
        if (ORBIT(ORQ((char *)s, 0), 47) && ORBIT(ORM((char *)s, 0), 15)) {
            ACTSendMailCorrect(self, 252);
        }
        if (ORBIT(ORQ((char *)s, 0), 49) && ORBIT(ORM((char *)s, 0), 17)) {
            ACTSendMailCorrect(self, 254);
        }
        if (ORBIT(ORQ((char *)s, 0), 50) && ORBIT(ORM((char *)s, 0), 18)) {
            ACTSendMailCorrect(self, 255);
        }
    }
    if (ORBIT(ORQ((char *)s, 0), 52) && ORBIT(ORM((char *)s, 0), 20)) {
        ACTSendMailCorrect(self, 181);
    }
    if (ORBIT(ORQ((char *)s, 0), 51) && ORBIT(ORM((char *)s, 0), 19)) {
        if (ACTGame_NoWeapon(self)) {
            ACTSendMailCorrect(self, 180);
        } else {
            ACTSendMailCorrect(self, 179);
        }
    }
    if (ORBIT(ORQ((char *)s, 0), 53) && ORBIT(ORM((char *)s, 0), 21)) {
        ACTSendMailCorrect(self, 307);
    }
    if (ORBIT(ORQ((char *)s, 0), 54) && ORBIT(ORM((char *)s, 0), 22)) {
        ACTSendMailCorrect(self, 330);
    }
    if (ORBIT(ORQ((char *)s, 0), 57) && ORBIT(ORM((char *)s, 0), 25)) {
        ACTSendMailCorrect(self, 187);
    }
    if (ORBIT(ORQ((char *)s, 0), 58) && ORBIT(ORM((char *)s, 0), 26)) {
        ACTSendMailCorrect(self, 188);
    }
    if (ORBIT(ORQ((char *)s, 0), 59) && ORBIT(ORM((char *)s, 0), 27)) {
        ACTSendMailCorrect(self, 190);
    }
    if (ORBIT(ORQ((char *)s, 0), 60) && ORBIT(ORM((char *)s, 0), 28)) {
        ActSendMail_WithAdditionalData(self, 238, self, (char *)&GOBJ_WORK(self)->effRec[0]);
    }
    if (ORBIT(ORQ((char *)s, 0), 61) && ORBIT(ORM((char *)s, 0), 29)) {
        ActSendMail_WithAdditionalData(self, 239, self, (char *)&GOBJ_WORK(self)->effRec[1]);
    }
    if (ORBIT(ORQ((char *)s, 0), 55) && ORBIT(ORM((char *)s, 0), 23)) {
        ACTSendMailCorrect(self, 156);
        ACTSendMailCorrect(self, 155);
    }
    if (ORBIT(ORQ((char *)s, 0), 56) && ORBIT(ORM((char *)s, 0), 24)) {
        ACTSendMailCorrect(self, 150);
    }
    if (ORBIT(ORQ((char *)s, 1), 2) && ORBIT(ORM((char *)s, 0), 34)) {
        ACTSendMailCorrect(self, 193);
        debug_StdPrintfDummy("shoal mail\n");
    } else if (s->actMode == 43) {
        ACTSendMailCorrect(self, 194);
    }
    if (ORBIT(ORQ((char *)s, 1), 3) && ORBIT(ORM((char *)s, 0), 35)) {
        ACTSendMailCorrect(self, 195);
    }
    if (ORBIT(ORQ((char *)s, 1), 4) && ORBIT(ORM((char *)s, 0), 36)) {
        ACTSendMailCorrect(self, 196);
    }
    if (ORBIT(ORQ((char *)s, 1), 5) && ORBIT(ORM((char *)s, 0), 37)) {
        ACTSendMailCorrect(self, 199);
    }
    if (ORBIT(ORQ((char *)s, 1), 6) && ORBIT(ORM((char *)s, 0), 38)) {
        ACTSendMailCorrect(self, 200);
    }
    if (ORBIT(ORQ((char *)s, 1), 7) && ORBIT(ORM((char *)s, 0), 39)) {
        ACTSendMailCorrect(self, 201);
    }
    if (ORBIT(ORQ((char *)s, 1), 26) && ORBIT(ORM((char *)s, 0), 58)) {
        ACTSendMailCorrect(self, 112);
    }
    if (ORBIT(ORQ((char *)s, 1), 28) && ORBIT(ORM((char *)s, 0), 60)) {
        ACTSendMailCorrect(self, 113);
    }
    if (ORBIT(ORQ((char *)s, 1), 30) && ORBIT(ORM((char *)s, 0), 62)) {
        ACTSendMailCorrect(self, 120);
    }
    if (ORBIT(ORQ((char *)s, 1), 29) && ORBIT(ORM((char *)s, 0), 61)) {
        ACTSendMailCorrect(self, 117);
    }
    if (ORBIT(ORQ((char *)s, 1), 31) && ORBIT(ORM((char *)s, 0), 63)) {
        ACTSendMailCorrect(self, 121);
    }
    if (ORBIT(ORQ((char *)s, 1), 32) && ORBIT(ORM((char *)s, 1), 0)) {
        ACTSendMailCorrect(self, 122);
    }
    if (ORBIT(ORQ((char *)s, 1), 33) && ORBIT(ORM((char *)s, 1), 1)) {
        ACTSendMailCorrect(self, 114);
    }
    if (ORBIT(ORQ((char *)s, 1), 34) && ORBIT(ORM((char *)s, 1), 2)) {
        ACTSendMailCorrect(self, 115);
        ACTSendMailCorrect(self, 116);
    }
    if (ORBIT(ORQ((char *)s, 1), 23) && ORBIT(ORM((char *)s, 0), 55)) {
        ACTSendMailCorrect(self, 109);
    }
    if (ORBIT(ORQ((char *)s, 1), 24) && ORBIT(ORM((char *)s, 0), 56)) {
        ACTSendMailCorrect(self, 110);
    }
    if (ORBIT(ORQ((char *)s, 1), 25) && ORBIT(ORM((char *)s, 0), 57)) {
        ACTSendMailCorrect(self, 111);
    }
    if (ORBIT(ORQ((char *)s, 0), 62) && ORBIT(ORM((char *)s, 0), 30)) {
        if (self == (girlGObj)) {
            /* PC port: with Players 2 outside GIRLREADYMOVE too, as the
               European ActOrientTest does */
            if (girlControlMode != 0 || GOBJ_ACT(self)->actMode == 112) {
                ACTSendMailCorrect(self, 105);
            }
        } else {
            ACTSendMailCorrect(self, 105);
        }
    }
    if (ORBIT(ORQ((char *)s, 0), 63) && ORBIT(ORM((char *)s, 0), 31)) {
        ACTSendMailCorrect(self, 292);
    }
    if (ORBIT(ORQ((char *)s, 1), 0) && ORBIT(ORM((char *)s, 0), 32)) {
        ACTSendMailCorrect(self, 293);
    }
    if (ORBIT(ORQ((char *)s, 1), 8) && ORBIT(ORM((char *)s, 0), 40)) {
        ACTSendMailCorrect(self, 285);
    }
    if (ORBIT(ORQ((char *)s, 1), 9) && ORBIT(ORM((char *)s, 0), 41)) {
        ACTSendMailCorrect(self, 286);
    }
    if (ORBIT(ORQ((char *)s, 1), 10) && ORBIT(ORM((char *)s, 0), 42)) {
        ACTSendMailCorrect(self, 287);
    }
    if (ORBIT(ORQ((char *)s, 1), 12) && ORBIT(ORM((char *)s, 0), 44)) {
        ACTSendMailCorrect(self, 288);
    }
    if (ORBIT(ORQ((char *)s, 1), 13) && ORBIT(ORM((char *)s, 0), 45)) {
        ACTSendMailCorrect(self, 288);
    }
    if (ORBIT(ORQ((char *)s, 1), 17) && ORBIT(ORM((char *)s, 0), 49)) {
        ACTSendMailCorrect(self, 285);
    }
    if (ORBIT(ORQ((char *)s, 1), 18) && ORBIT(ORM((char *)s, 0), 50)) {
        ACTSendMailCorrect(self, 286);
    }
    if (ORBIT(ORQ((char *)s, 1), 19) && ORBIT(ORM((char *)s, 0), 51)) {
        ACTSendMailCorrect(self, 287);
    }
    if (ORBIT(ORQ((char *)s, 1), 44) && ORBIT(ORM((char *)s, 1), 12)) {
        ACTSendMailCorrect(self, 40);
    }
    if (ORBIT(ORQ((char *)s, 1), 45) && ORBIT(ORM((char *)s, 1), 13)) {
        ACTSendMailCorrect(self, 125);
    }
    if (ORBIT(ORQ((char *)s, 1), 50) && ORBIT(ORM((char *)s, 1), 18)) {
        ACTSendMailCorrect(self, 269);
    }
    if (ORBIT(ORQ((char *)s, 1), 51) && ORBIT(ORM((char *)s, 1), 19)) {
        ACTSendMailCorrect(self, 267);
    }
    if (ORBIT(ORQ((char *)s, 1), 42) && ORBIT(ORM((char *)s, 1), 10)) {
        ACTSendMailCorrect(self, 272);
    }
    if (ORBIT(ORQ((char *)s, 1), 43) && ORBIT(ORM((char *)s, 1), 11)) {
        ACTSendMailCorrect(self, 132);
    }
    if (ORBIT(ORQ((char *)s, 1), 35) && ORBIT(ORM((char *)s, 1), 3)) {
        ACTSendMailCorrect(self, 267);
    }
    if (ORBIT(ORQ((char *)s, 1), 36) && ORBIT(ORM((char *)s, 1), 4)) {
        ACTSendMailCorrect(self, 271);
    }
    if (ORBIT(ORQ((char *)s, 1), 37) && ORBIT(ORM((char *)s, 1), 5)) {
        ACTSendMailCorrect(self, 277);
    }
    if (ORBIT(ORQ((char *)s, 1), 38) && ORBIT(ORM((char *)s, 1), 6)) {
        ACTSendMailCorrect(self, 278);
    }
    if (ORBIT(ORQ((char *)s, 1), 39) && ORBIT(ORM((char *)s, 1), 7)) {
        ACTSendMailCorrect(self, 279);
    }
    if (ORBIT(ORQ((char *)s, 1), 40) && ORBIT(ORM((char *)s, 1), 8)) {
        ACTSendMailCorrect(self, 280);
    }
    if (ORBIT(ORQ((char *)s, 1), 41) && ORBIT(ORM((char *)s, 1), 9)) {
        ACTSendMailCorrect(self, 281);
    }
    if (ORBIT(ORQ((char *)s, 1), 59) && ORBIT(ORM((char *)s, 1), 27)) {
        ACTSendMailCorrect(self, 81);
    }
    if (ORBIT(ORQ((char *)s, 1), 60) && ORBIT(ORM((char *)s, 1), 28)) {
        ACTSendMailCorrect(self, 82);
    }
    if (ORBIT(ORQ((char *)s, 1), 56) && ORBIT(ORM((char *)s, 1), 24)) {
        ACTSendMailCorrect(self, 66);
        s->orientMot = 92;
    }
    if (ORBIT(ORQ((char *)s, 1), 57) && ORBIT(ORM((char *)s, 1), 25)) {
        ACTSendMailCorrect(self, 66);
        s->orientMot = 94;
    }
    if (ORBIT(ORQ((char *)s, 1), 58) && ORBIT(ORM((char *)s, 1), 26)) {
        ACTSendMailCorrect(self, 66);
        s->orientMot = 96;
    }
    if (ORBIT(ORQ((char *)s, 1), 14) && ORBIT(ORM((char *)s, 0), 46)) {
        ACTSendMailCorrect(self, 289);
    }
    if (ORBIT(ORQ((char *)s, 1), 15) && ORBIT(ORM((char *)s, 0), 47)) {
        ACTSendMailCorrect(self, 290);
    }
    if (ORBIT(ORQ((char *)s, 1), 16) && ORBIT(ORM((char *)s, 0), 48)) {
        ACTSendMailCorrect(self, 291);
    }
    if (ORBIT(ORQ((char *)s, 1), 49) && ORBIT(ORM((char *)s, 1), 17)) {
        ACTSendMailCorrect(self, 149);
    }
    if (ORBIT(ORQ((char *)s, 1), 61) && ORBIT(ORM((char *)s, 1), 29)) {
        ACTSendMailCorrect(self, 343);
    }
    if (ORBIT(ORQ((char *)s, 1), 62) && ORBIT(ORM((char *)s, 1), 30)) {
        vel = s->env.cliffOrient;
        sceVu0ScaleVector(v0, vel, -sceVu0InnerProduct((char *)GOBJ_SUB(self)->root.move, vel));
        sceVu0AddVector((char *)GOBJ_SUB(self)->root.move, (char *)GOBJ_SUB(self)->root.move, v0);
        SetRootPosition(self, s->env.cliffBackPos);
    }
    if (GOBJ_ACT(self)->enemy->stoneLevel > 0) {
        ACTSendMailCorrect(self, 102);
    }
    if (((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags2 >> 2) & 1) {
        if (GetMotionFrameFlag1(self)) {
            memset(&w1, 0, sizeof(w1));
            GetSkeltonPosition(sk1, self, 51);
            GetSkeltonPosition(sk2, self, 47);
            sceVu0AddVector(p1, sk1, sk2);
            sceVu0ScaleVector(p1, p1, 0.5f);
            p1[1] = p1[1] + 50.0f;
            sceVu0ScaleVector(d1, test_CURRENTORIENT(self), 50.0f);
            sceVu0AddVector(w1.pt[0], p1, d1);
            sceVu0ScaleVector(d1, test_CURRENTORIENT(self), -50.0f);
            sceVu0AddVector(w1.pt[1], p1, d1);
            w1.radius = 0.0f;
            ClipWall(&w1);
            if (CompareAttribute(w1.attr, 0x2000)) {
                ACTSendMailCorrect(self, 294);
            }
            if (CompareAttribute(w1.attr, 0x20000)) {
                if (s->addData != 0) {
                    if (GetMotionFrameFlag1(self)) {
                        char *ext;
                        ((MotOriTarget *)s->addData)->wall = w1.wall;
                        ext = (char *)s->addData;
                        ActSendMail_WithAdditionalData(self, 270, self, ext);
                    }
                }
            }
        }
    }
    if (((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags2 >> 3) & 1) {
        memset(&w2, 0, sizeof(w2));
        near = 0;
        p2[0] = test_CURRENTROOT(self)[0];
        p2[1] = test_CURRENTROOT(self)[1];
        p2[2] = test_CURRENTROOT(self)[2];
        sceVu0ScaleVector(d2, test_CURRENTORIENT(self), -50.0f);
        sceVu0AddVector(w2.pt[0], p2, d2);
        sceVu0ScaleVector(d2, test_CURRENTORIENT(self), 50.0f);
        sceVu0AddVector(w2.pt[1], p2, d2);
        hitA = 0;
        w2.radius = 0.0f;
        ClipWall(&w2);
        hitB = 0;
        if (CompareAttribute(w2.attr, 0x400)) {
            hitA = 1;
        }
        if (CompareAttribute(w2.attr, 0xC000)) {
            hitB = 1;
        }
        if (hitA || hitB) {
            GetCollisCenterPositionSimple(c1, w2.wall.o.obj, w2.wall.elem);
            if (_DistxzSqGV(c1, test_CURRENTROOT(self)) < 400.0f) {
                near = 1;
            }
        }
        if (hitA && near) {
            ACTSendMailCorrect(self, 128);
        }
        if (hitB && near) {
            ACTSendMailCorrect(self, 279);
        }
    }
}

static void ActOrientTest_aug6(GObj *self)
{
    float v0[4];
    ClipWork w1;
    float p1[4];
    float sk1[4];
    float sk2[4];
    float d1[4];
    ClipWork w2;
    float p2[4];
    float d2[4];
    float c1[4];
    ClipWork w3;
    float sk3[4];
    float sk4[4];
    float d3[4];
    float ow[4];
    Act *s = GOBJ_ACT(self);
    float *vel;
    int hitA;
    int hitB;
    int near;
    int i;

    if (ORBIT(ORQ((char *)s, 0), 38) && ORBIT(ORM((char *)s, 0), 38)) {
        ACTSendMailCorrect(self, 174);
    }
    if (ORBIT(ORQ((char *)s, 0), 39) && ORBIT(ORM((char *)s, 0), 39)) {
        ACTSendMailCorrect(self, 175);
    }
    if (ORBIT(ORQ((char *)s, 0), 40) && ORBIT(ORM((char *)s, 0), 40)) {
        ACTSendMailCorrect(self, 176);
    }
    if (ORBIT(ORQ((char *)s, 0), 41) && ORBIT(ORM((char *)s, 0), 41)) {
        ACTSendMailCorrect(self, 177);
    }
    if (ORBIT(ORQ((char *)s, 0), 42) && ORBIT(ORM((char *)s, 0), 42)) {
        if (((struct GObj *)self)->kind == 4) {
            if (rand() & 1) {
                ACTSendMailCorrect(self, 186);
            } else {
                ACTSendMailCorrect(self, 188);
            }
        } else {
            ACTSendMailCorrect(self, 186);
        }
    }
    if (ORBIT(ORQ((char *)s, 0), 43) && ORBIT(ORM((char *)s, 0), 43)) {
        ACTSendMailCorrect(self, 187);
    }
    if (ORBIT(ORQ((char *)s, 0), 48) && ORBIT(ORM((char *)s, 0), 48)) {
        ACTSendMailCorrect(self, 260);
    }
    if (self != boyGObj) {
        if (ORBIT(ORQ((char *)s, 0), 47) && ORBIT(ORM((char *)s, 0), 47)) {
            ACTSendMailCorrect(self, 259);
        }
        if (ORBIT(ORQ((char *)s, 0), 49) && ORBIT(ORM((char *)s, 0), 49)) {
            ACTSendMailCorrect(self, 261);
        }
        if (ORBIT(ORQ((char *)s, 0), 50) && ORBIT(ORM((char *)s, 0), 50)) {
            ACTSendMailCorrect(self, 262);
        }
    }
    if (ORBIT(ORQ((char *)s, 0), 52) && ORBIT(ORM((char *)s, 0), 52)) {
        ACTSendMailCorrect(self, 184);
    }
    if (ORBIT(ORQ((char *)s, 0), 51) && ORBIT(ORM((char *)s, 0), 51)) {
        if (ACTGame_NoWeapon(self)) {
            ACTSendMailCorrect(self, 183);
        } else {
            ACTSendMailCorrect(self, 182);
        }
    }
    if (ORBIT(ORQ((char *)s, 0), 53) && ORBIT(ORM((char *)s, 0), 53)) {
        ACTSendMailCorrect(self, 316);
    }
    if (ORBIT(ORQ((char *)s, 0), 54) && ORBIT(ORM((char *)s, 0), 54)) {
        ACTSendMailCorrect(self, 346);
    }
    if (ORBIT(ORQ((char *)s, 0), 57) && ORBIT(ORM((char *)s, 0), 57)) {
        ACTSendMailCorrect(self, 190);
    }
    if (ORBIT(ORQ((char *)s, 0), 58) && ORBIT(ORM((char *)s, 0), 58)) {
        ACTSendMailCorrect(self, 191);
    }
    if (ORBIT(ORQ((char *)s, 0), 59) && ORBIT(ORM((char *)s, 0), 59)) {
        ACTSendMailCorrect(self, 193);
    }
    if (ORBIT(ORQ((char *)s, 0), 60) && ORBIT(ORM((char *)s, 0), 60)) {
        ACTSendMailCorrect(self, 194);
    }
    if (ORBIT(ORQ((char *)s, 0), 61) && ORBIT(ORM((char *)s, 0), 61)) {
        ActSendMail_WithAdditionalData(
            self, 244, self, (char *)&GOBJ_WORK(self)->effRec[0]); /* PC port: the host record's */
    }
    if (ORBIT(ORQ((char *)s, 0), 62) && ORBIT(ORM((char *)s, 0), 62)) {
        ActSendMail_WithAdditionalData(
            self, 245, self, (char *)&GOBJ_WORK(self)->effRec[1]); /* PC port: the host record's */
    }
    if (ORBIT(ORQ((char *)s, 0), 63) && ORBIT(ORM((char *)s, 0), 63)) {
        ActSendMail_WithAdditionalData(
            self, 246, self, (char *)&GOBJ_WORK(self)->effRec[2]); /* PC port: the host record's */
    }
    if (ORBIT(ORQ((char *)s, 0), 55) && ORBIT(ORM((char *)s, 0), 55)) {
        ACTSendMailCorrect(self, 159);
        ACTSendMailCorrect(self, 158);
    }
    if (ORBIT(ORQ((char *)s, 0), 56) && ORBIT(ORM((char *)s, 0), 56)) {
        ACTSendMailCorrect(self, 152);
    }
    if (ORBIT(ORQ((char *)s, 1), 4) && ORBIT(ORM((char *)s, 1), 4)) {
        ACTSendMailCorrect(self, 197);
        debug_StdPrintfDummy("shoal mail\n");
    } else if (s->actMode == 43) {
        ACTSendMailCorrect(self, 198);
    }
    if (ORBIT(ORQ((char *)s, 1), 5) && ORBIT(ORM((char *)s, 1), 5)) {
        ACTSendMailCorrect(self, 199);
    }
    if (ORBIT(ORQ((char *)s, 1), 6) && ORBIT(ORM((char *)s, 1), 6)) {
        ACTSendMailCorrect(self, 200);
    }
    if (ORBIT(ORQ((char *)s, 1), 7) && ORBIT(ORM((char *)s, 1), 7)) {
        ACTSendMailCorrect(self, 201);
    }
    if (ORBIT(ORQ((char *)s, 1), 8) && ORBIT(ORM((char *)s, 1), 8)) {
        ACTSendMailCorrect(self, 204);
    }
    if (ORBIT(ORQ((char *)s, 1), 9) && ORBIT(ORM((char *)s, 1), 9)) {
        ACTSendMailCorrect(self, 205);
    }
    if (ORBIT(ORQ((char *)s, 1), 10) && ORBIT(ORM((char *)s, 1), 10)) {
        ACTSendMailCorrect(self, 206);
    }
    if (ORBIT(ORQ((char *)s, 1), 29) && ORBIT(ORM((char *)s, 1), 29)) {
        ACTSendMailCorrect(self, 113);
    }
    if (ORBIT(ORQ((char *)s, 1), 31) && ORBIT(ORM((char *)s, 1), 31)) {
        ACTSendMailCorrect(self, 114);
    }
    if (ORBIT(ORQ((char *)s, 1), 33) && ORBIT(ORM((char *)s, 1), 33)) {
        ACTSendMailCorrect(self, 121);
    }
    if (ORBIT(ORQ((char *)s, 1), 32) && ORBIT(ORM((char *)s, 1), 32)) {
        ACTSendMailCorrect(self, 118);
    }
    if (ORBIT(ORQ((char *)s, 1), 34) && ORBIT(ORM((char *)s, 1), 34)) {
        ACTSendMailCorrect(self, 122);
    }
    if (ORBIT(ORQ((char *)s, 1), 35) && ORBIT(ORM((char *)s, 1), 35)) {
        ACTSendMailCorrect(self, 123);
    }
    if (ORBIT(ORQ((char *)s, 1), 36) && ORBIT(ORM((char *)s, 1), 36)) {
        ACTSendMailCorrect(self, 115);
    }
    if (ORBIT(ORQ((char *)s, 1), 37) && ORBIT(ORM((char *)s, 1), 37)) {
        ACTSendMailCorrect(self, 116);
        ACTSendMailCorrect(self, 117);
    }
    if (ORBIT(ORQ((char *)s, 1), 26) && ORBIT(ORM((char *)s, 1), 26)) {
        ACTSendMailCorrect(self, 110);
    }
    if (ORBIT(ORQ((char *)s, 1), 27) && ORBIT(ORM((char *)s, 1), 27)) {
        ACTSendMailCorrect(self, 111);
    }
    if (ORBIT(ORQ((char *)s, 1), 28) && ORBIT(ORM((char *)s, 1), 28)) {
        ACTSendMailCorrect(self, 112);
    }
    if (ORBIT(ORQ((char *)s, 1), 0) && ORBIT(ORM((char *)s, 1), 0)) {
        if (self == (girlGObj)) {
            /* PC port: with Players 2 outside GIRLREADYMOVE too, as the
               European ActOrientTest does */
            if (girlControlMode != 0 || GOBJ_ACT(self)->actMode == 113) {
                ACTSendMailCorrect(self, 106);
            }
        } else {
            ACTSendMailCorrect(self, 106);
        }
    }
    if (ORBIT(ORQ((char *)s, 1), 1) && ORBIT(ORM((char *)s, 1), 1)) {
        ACTSendMailCorrect(self, 301);
    }
    if (ORBIT(ORQ((char *)s, 1), 2) && ORBIT(ORM((char *)s, 1), 2)) {
        ACTSendMailCorrect(self, 302);
    }
    if (ORBIT(ORQ((char *)s, 1), 11) && ORBIT(ORM((char *)s, 1), 11)) {
        ACTSendMailCorrect(self, 294);
    }
    if (ORBIT(ORQ((char *)s, 1), 12) && ORBIT(ORM((char *)s, 1), 12)) {
        ACTSendMailCorrect(self, 295);
    }
    if (ORBIT(ORQ((char *)s, 1), 13) && ORBIT(ORM((char *)s, 1), 13)) {
        ACTSendMailCorrect(self, 296);
    }
    if (ORBIT(ORQ((char *)s, 1), 15) && ORBIT(ORM((char *)s, 1), 15)) {
        ACTSendMailCorrect(self, 297);
    }
    if (ORBIT(ORQ((char *)s, 1), 16) && ORBIT(ORM((char *)s, 1), 16)) {
        ACTSendMailCorrect(self, 297);
    }
    if (ORBIT(ORQ((char *)s, 1), 20) && ORBIT(ORM((char *)s, 1), 20)) {
        ACTSendMailCorrect(self, 294);
    }
    if (ORBIT(ORQ((char *)s, 1), 21) && ORBIT(ORM((char *)s, 1), 21)) {
        ACTSendMailCorrect(self, 295);
    }
    if (ORBIT(ORQ((char *)s, 1), 22) && ORBIT(ORM((char *)s, 1), 22)) {
        ACTSendMailCorrect(self, 296);
    }
    if (ORBIT(ORQ((char *)s, 1), 47) && ORBIT(ORM((char *)s, 1), 47)) {
        ACTSendMailCorrect(self, 41);
    }
    if (ORBIT(ORQ((char *)s, 1), 48) && ORBIT(ORM((char *)s, 1), 48)) {
        ACTSendMailCorrect(self, 126);
    }
    if (ORBIT(ORQ((char *)s, 1), 49) && ORBIT(ORM((char *)s, 1), 49)) {
        ACTSendMailCorrect(self, 127);
    }
    if (ORBIT(ORQ((char *)s, 1), 54) && ORBIT(ORM((char *)s, 1), 54)) {
        ACTSendMailCorrect(self, 277);
    }
    if (ORBIT(ORQ((char *)s, 1), 55) && ORBIT(ORM((char *)s, 1), 55)) {
        ACTSendMailCorrect(self, 279);
    }
    if ((ORBIT(ORQ((char *)s, 1), 56) && ORBIT(ORM((char *)s, 1), 56)) ||
        (ORBIT(ORQ((char *)s, 1), 57) && ORBIT(ORM((char *)s, 1), 57))) {
        ACTSendMailCorrect(self, 275);
    }
    if (ORBIT(ORQ((char *)s, 1), 45) && ORBIT(ORM((char *)s, 1), 45)) {
        ACTSendMailCorrect(self, 281);
    }
    if (ORBIT(ORQ((char *)s, 1), 46) && ORBIT(ORM((char *)s, 1), 46)) {
        ACTSendMailCorrect(self, 134);
    }
    if (ORBIT(ORQ((char *)s, 1), 38) && ORBIT(ORM((char *)s, 1), 38)) {
        ACTSendMailCorrect(self, 275);
    }
    if (ORBIT(ORQ((char *)s, 1), 39) && ORBIT(ORM((char *)s, 1), 39)) {
        ACTSendMailCorrect(self, 280);
    }
    if (ORBIT(ORQ((char *)s, 1), 40) && ORBIT(ORM((char *)s, 1), 40)) {
        ACTSendMailCorrect(self, 286);
    }
    if (ORBIT(ORQ((char *)s, 1), 41) && ORBIT(ORM((char *)s, 1), 41)) {
        ACTSendMailCorrect(self, 287);
    }
    if (ORBIT(ORQ((char *)s, 1), 42) && ORBIT(ORM((char *)s, 1), 42)) {
        ACTSendMailCorrect(self, 288);
    }
    if (ORBIT(ORQ((char *)s, 1), 43) && ORBIT(ORM((char *)s, 1), 43)) {
        ACTSendMailCorrect(self, 289);
    }
    if (ORBIT(ORQ((char *)s, 1), 44) && ORBIT(ORM((char *)s, 1), 44)) {
        ACTSendMailCorrect(self, 290);
    }
    if (ORBIT(ORQ((char *)s, 2), 1) && ORBIT(ORM((char *)s, 2), 1)) {
        ACTSendMailCorrect(self, 82);
    }
    if (ORBIT(ORQ((char *)s, 2), 2) && ORBIT(ORM((char *)s, 2), 2)) {
        ACTSendMailCorrect(self, 83);
    }
    if (ORBIT(ORQ((char *)s, 1), 62) && ORBIT(ORM((char *)s, 1), 62)) {
        ACTSendMailCorrect(self, 67);
        s->orientMot = 95;
    }
    if (ORBIT(ORQ((char *)s, 1), 63) && ORBIT(ORM((char *)s, 1), 63)) {
        ACTSendMailCorrect(self, 67);
        s->orientMot = 97;
    }
    if (ORBIT(ORQ((char *)s, 2), 0) && ORBIT(ORM((char *)s, 2), 0)) {
        ACTSendMailCorrect(self, 67);
        s->orientMot = 99;
    }
    if (ORBIT(ORQ((char *)s, 1), 17) && ORBIT(ORM((char *)s, 1), 17)) {
        ACTSendMailCorrect(self, 298);
    }
    if (ORBIT(ORQ((char *)s, 1), 18) && ORBIT(ORM((char *)s, 1), 18)) {
        ACTSendMailCorrect(self, 299);
    }
    if (ORBIT(ORQ((char *)s, 1), 19) && ORBIT(ORM((char *)s, 1), 19)) {
        ACTSendMailCorrect(self, 300);
    }
    if (ORBIT(ORQ((char *)s, 1), 53) && ORBIT(ORM((char *)s, 1), 53)) {
        ACTSendMailCorrect(self, 151);
    }
    if (ORBIT(ORQ((char *)s, 2), 3) && ORBIT(ORM((char *)s, 2), 3)) {
        ACTSendMailCorrect(self, 359);
    }
    if (ORBIT(ORQ((char *)s, 2), 4) && ORBIT(ORM((char *)s, 2), 4)) {
        vel = s->env.cliffOrient;
        sceVu0ScaleVector(v0, vel, -sceVu0InnerProduct((char *)GOBJ_SUB(self)->root.move, vel));
        sceVu0AddVector((char *)GOBJ_SUB(self)->root.move, (char *)GOBJ_SUB(self)->root.move, v0);
        SetRootPosition(self, s->env.cliffBackPos);
    }
    if (GOBJ_ACT(self)->enemy->stoneLevel > 0) {
        ACTSendMailCorrect(self, 103);
    }
    if (((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags2 >> 2) & 1) {
        if (GetMotionFrameFlag1(self)) {
            memset(&w1, 0, sizeof(w1));
            GetSkeltonPosition(sk1, self, 51);
            GetSkeltonPosition(sk2, self, 47);
            sceVu0AddVector(p1, sk1, sk2);
            sceVu0ScaleVector(p1, p1, 0.5f);
            p1[1] = p1[1] + 50.0f;
            sceVu0ScaleVector(d1, test_CURRENTORIENT(self), 50.0f);
            sceVu0AddVector(w1.pt[0], p1, d1);
            sceVu0ScaleVector(d1, test_CURRENTORIENT(self), -50.0f);
            sceVu0AddVector(w1.pt[1], p1, d1);
            w1.radius = 0.0f;
            ClipWall(&w1);
            if (CompareAttribute(w1.attr, 0x2000)) {
                ACTSendMailCorrect(self, 303);
            }
            if (CompareAttribute(w1.attr, 0x20000)) {
                if (s->addData != 0) {
                    if (GetMotionFrameFlag1(self)) {
                        char *ext;
                        ((MotOriTarget *)s->addData)->wall = w1.wall;
                        ext = (char *)s->addData;
                        ActSendMail_WithAdditionalData(self, 278, self, ext);
                    }
                }
            }
        }
    }
    if (((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags2 >> 3) & 1) {
        memset(&w2, 0, sizeof(w2));
        near = 0;
        p2[0] = test_CURRENTROOT(self)[0];
        p2[1] = test_CURRENTROOT(self)[1];
        p2[2] = test_CURRENTROOT(self)[2];
        sceVu0ScaleVector(d2, test_CURRENTORIENT(self), -50.0f);
        sceVu0AddVector(w2.pt[0], p2, d2);
        sceVu0ScaleVector(d2, test_CURRENTORIENT(self), 50.0f);
        sceVu0AddVector(w2.pt[1], p2, d2);
        hitA = 0;
        w2.radius = 0.0f;
        ClipWall(&w2);
        hitB = 0;
        if (CompareAttribute(w2.attr, 0x400)) {
            hitA = 1;
        }
        if (CompareAttribute(w2.attr, 0xC000)) {
            hitB = 1;
        }
        if (hitA || hitB) {
            GetCollisCenterPositionSimple(c1, w2.wall.o.obj, w2.wall.elem);
            if (_DistxzSqGV(c1, test_CURRENTROOT(self)) < 400.0f) {
                near = 1;
            }
        }
        if (hitA && near) {
            ACTSendMailCorrect(self, 130);
        }
        if (hitB && near) {
            ACTSendMailCorrect(self, 288);
        }
    }
    for (i = 0; i < 2; i++) {
        if (((int)(s->flags20.ll >> 18) & 1) == 0) {
            continue;
        }
        memset(&w3, 0, sizeof(w3));
        GetSkeltonPosition(sk3, self, 51);
        GetSkeltonPosition(sk4, self, 47);
        sceVu0AddVector(c1, sk3, sk4);
        sceVu0ScaleVector(c1, c1, 0.5f);
        c1[1] = c1[1] + 50.0f;
        sceVu0ScaleVector(d3, test_CURRENTORIENT(self), 50.0f);
        _ApplyRyGV(d3, -1.5707964f);
        sceVu0AddVector(w3.pt[0], c1, d3);
        sceVu0ScaleVector(d3, test_CURRENTORIENT(self), 50.0f);
        _ApplyRyGV(d3, 1.5707964f);
        sceVu0AddVector(w3.pt[1], c1, d3);
        if (i == 1) {
            SwapGV(w3.pt[0], w3.pt[1]);
        }
        w3.radius = 0.0f;
        ClipWall(&w3);
        if (w3.wall.elem != 0) {
            GetOrientOfWall(ow, w3.wall.elem, &w3.wall.o);
            SetMotionDirection(self, ow);
            s->flags20.ll &= ~(1ULL << 18);
            return;
        }
    }
}

static void ActOrientTest(GObj *self)
{
    if (ICO_JUN26) {
        ActOrientTest_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        ActOrientTest_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        ActOrientTest_aug6(self);
        return;
    }
    float v0[4];
    ClipWork w1;
    float p1[4];
    float sk1[4];
    float sk2[4];
    float d1[4];
    ClipWork w2;
    float p2[4];
    float d2[4];
    float c1[4];
    ClipWork w3;
    float sk3[4];
    float sk4[4];
    float d3[4];
    float ow[4];
    Act *s = GOBJ_ACT(self);
    float *vel;
    int hitA;
    int hitB;
    int near;
    int i;

    if (ORBIT(ORQ((char *)s, 0), 39) && ORBIT(ORM((char *)s, 0), 39)) {
        ACTSendMailCorrect(self, 189);
    }
    if (ORBIT(ORQ((char *)s, 0), 40) && ORBIT(ORM((char *)s, 0), 40)) {
        ACTSendMailCorrect(self, 191);
    }
    if (ORBIT(ORQ((char *)s, 0), 41) && ORBIT(ORM((char *)s, 0), 41)) {
        ACTSendMailCorrect(self, 192);
    }
    if (ORBIT(ORQ((char *)s, 0), 42) && ORBIT(ORM((char *)s, 0), 42)) {
        ACTSendMailCorrect(self, 193);
    }
    if (ORBIT(ORQ((char *)s, 0), 43) && ORBIT(ORM((char *)s, 0), 43)) {
        ACTSendMailCorrect(self, 197);
    }
    if (ORBIT(ORQ((char *)s, 0), 44) && ORBIT(ORM((char *)s, 0), 44)) {
        if (((struct GObj *)self)->kind == 4) {
            if (rand() & 1) {
                ACTSendMailCorrect(self, 205);
            } else {
                ACTSendMailCorrect(self, 207);
            }
        } else {
            ACTSendMailCorrect(self, 205);
        }
    }
    if (ORBIT(ORQ((char *)s, 0), 45) && ORBIT(ORM((char *)s, 0), 45)) {
        ACTSendMailCorrect(self, 206);
    }
    if (ORBIT(ORQ((char *)s, 0), 51) && ORBIT(ORM((char *)s, 0), 51)) {
        ACTSendMailCorrect(self, 279);
    }
    if (self != boyGObj) {
        if (ORBIT(ORQ((char *)s, 0), 50) && ORBIT(ORM((char *)s, 0), 50)) {
            ACTSendMailCorrect(self, 278);
        }
        if (ORBIT(ORQ((char *)s, 0), 52) && ORBIT(ORM((char *)s, 0), 52)) {
            ACTSendMailCorrect(self, 280);
        }
        if (ORBIT(ORQ((char *)s, 0), 53) && ORBIT(ORM((char *)s, 0), 53)) {
            ACTSendMailCorrect(self, 281);
        }
    }
    if (ORBIT(ORQ((char *)s, 0), 55) && ORBIT(ORM((char *)s, 0), 55)) {
        ACTSendMailCorrect(self, 203);
    }
    if (ORBIT(ORQ((char *)s, 0), 54) && ORBIT(ORM((char *)s, 0), 54)) {
        if (ACTGame_NoWeapon(self)) {
            ACTSendMailCorrect(self, 202);
        } else {
            ACTSendMailCorrect(self, 201);
        }
    }
    if (ORBIT(ORQ((char *)s, 0), 56) && ORBIT(ORM((char *)s, 0), 56)) {
        ACTSendMailCorrect(self, 348);
    }
    if (ORBIT(ORQ((char *)s, 0), 58) && ORBIT(ORM((char *)s, 0), 58)) {
        ACTSendMailCorrect(self, 345);
    }
    if (ORBIT(ORQ((char *)s, 0), 57) && ORBIT(ORM((char *)s, 0), 57)) {
        ACTSendMailCorrect(self, 346);
    }
    if (ORBIT(ORQ((char *)s, 0), 59) && ORBIT(ORM((char *)s, 0), 59)) {
        ACTSendMailCorrect(self, 378);
    }
    if (ORBIT(ORQ((char *)s, 0), 62) && ORBIT(ORM((char *)s, 0), 62)) {
        ACTSendMailCorrect(self, 209);
    }
    if (ORBIT(ORQ((char *)s, 0), 63) && ORBIT(ORM((char *)s, 0), 63)) {
        ACTSendMailCorrect(self, 210);
    }
    if (ORBIT(ORQ((char *)s, 1), 0) && ORBIT(ORM((char *)s, 1), 0)) {
        ACTSendMailCorrect(self, 212);
    }
    if (ORBIT(ORQ((char *)s, 1), 1) && ORBIT(ORM((char *)s, 1), 1)) {
        ACTSendMailCorrect(self, 213);
    }
    if (ORBIT(ORQ((char *)s, 1), 2) && ORBIT(ORM((char *)s, 1), 2)) {
        ActSendMail_WithAdditionalData(
            self, 263, self,
            ICO_RAWP(char *, ACTWORK(self), 2064, (char *)&GOBJ_WORK(self)->effRec[0]));
    }
    if (ORBIT(ORQ((char *)s, 1), 3) && ORBIT(ORM((char *)s, 1), 3)) {
        ActSendMail_WithAdditionalData(
            self, 264, self,
            ICO_RAWP(char *, ACTWORK(self), 2112, (char *)&GOBJ_WORK(self)->effRec[1]));
    }
    if (ORBIT(ORQ((char *)s, 1), 4) && ORBIT(ORM((char *)s, 1), 4)) {
        ActSendMail_WithAdditionalData(
            self, 265, self,
            ICO_RAWP(char *, ACTWORK(self), 2160, (char *)&GOBJ_WORK(self)->effRec[2]));
    }
    if (ORBIT(ORQ((char *)s, 0), 60) && ORBIT(ORM((char *)s, 0), 60)) {
        ACTSendMailCorrect(self, 174);
        ACTSendMailCorrect(self, 173);
    }
    if (ORBIT(ORQ((char *)s, 0), 61) && ORBIT(ORM((char *)s, 0), 61)) {
        ACTSendMailCorrect(self, 166);
    }
    if (ORBIT(ORQ((char *)s, 1), 11) && ORBIT(ORM((char *)s, 1), 11)) {
        ACTSendMailCorrect(self, 216);
        debug_StdPrintfDummy("shoal mail\n");
    } else if (s->actMode == 43) {
        ACTSendMailCorrect(self, 217);
    }
    if (ORBIT(ORQ((char *)s, 1), 12) && ORBIT(ORM((char *)s, 1), 12)) {
        ACTSendMailCorrect(self, 218);
    }
    if (ORBIT(ORQ((char *)s, 1), 13) && ORBIT(ORM((char *)s, 1), 13)) {
        ACTSendMailCorrect(self, 219);
    }
    if (ORBIT(ORQ((char *)s, 1), 14) && ORBIT(ORM((char *)s, 1), 14)) {
        ACTSendMailCorrect(self, 220);
    }
    if (ORBIT(ORQ((char *)s, 1), 15) && ORBIT(ORM((char *)s, 1), 15)) {
        ACTSendMailCorrect(self, 223);
    }
    if (ORBIT(ORQ((char *)s, 1), 16) && ORBIT(ORM((char *)s, 1), 16)) {
        ACTSendMailCorrect(self, 224);
    }
    if (ORBIT(ORQ((char *)s, 1), 17) && ORBIT(ORM((char *)s, 1), 17)) {
        ACTSendMailCorrect(self, 225);
    }
    if (ORBIT(ORQ((char *)s, 1), 36) && ORBIT(ORM((char *)s, 1), 36)) {
        ACTSendMailCorrect(self, 121);
    }
    if (ORBIT(ORQ((char *)s, 1), 38) && ORBIT(ORM((char *)s, 1), 38)) {
        ACTSendMailCorrect(self, 122);
    }
    if (ORBIT(ORQ((char *)s, 1), 40) && ORBIT(ORM((char *)s, 1), 40)) {
        ACTSendMailCorrect(self, 130);
    }
    if (ORBIT(ORQ((char *)s, 1), 39) && ORBIT(ORM((char *)s, 1), 39)) {
        ACTSendMailCorrect(self, 127);
    }
    if (ORBIT(ORQ((char *)s, 1), 41) && ORBIT(ORM((char *)s, 1), 41)) {
        ACTSendMailCorrect(self, 131);
    }
    if (ORBIT(ORQ((char *)s, 1), 42) && ORBIT(ORM((char *)s, 1), 42)) {
        ACTSendMailCorrect(self, 132);
    }
    if (ORBIT(ORQ((char *)s, 1), 43) && ORBIT(ORM((char *)s, 1), 43)) {
        ACTSendMailCorrect(self, 123);
    }
    if (ORBIT(ORQ((char *)s, 1), 44) && ORBIT(ORM((char *)s, 1), 44)) {
        ACTSendMailCorrect(self, 124);
        ACTSendMailCorrect(self, 125);
    }
    if (ORBIT(ORQ((char *)s, 1), 33) && ORBIT(ORM((char *)s, 1), 33)) {
        ACTSendMailCorrect(self, 118);
    }
    if (ORBIT(ORQ((char *)s, 1), 34) && ORBIT(ORM((char *)s, 1), 34)) {
        ACTSendMailCorrect(self, 119);
    }
    if (ORBIT(ORQ((char *)s, 1), 35) && ORBIT(ORM((char *)s, 1), 35)) {
        ACTSendMailCorrect(self, 120);
    }
    if (ORBIT(ORQ((char *)s, 1), 5) && ORBIT(ORM((char *)s, 1), 5)) {
        if (self == (girlGObj)) {
            int ok = 0;
            if (girlControlMode != 0) {
                ok = 1;
            }
            if (GOBJ_ACT(self)->actMode == 0x75) {
                ok = 1;
            }
            if (ok) {
                ACTSendMailCorrect(self, 114);
            }
        } else {
            ACTSendMailCorrect(self, 114);
        }
    }
    if (ORBIT(ORQ((char *)s, 1), 6) && ORBIT(ORM((char *)s, 1), 6)) {
        ACTSendMailCorrect(self, 324);
    }
    if (ORBIT(ORQ((char *)s, 1), 8) && ORBIT(ORM((char *)s, 1), 8)) {
        ACTSendMailCorrect(self, 325);
    }
    if (ORBIT(ORQ((char *)s, 1), 7) && ORBIT(ORM((char *)s, 1), 7)) {
        ACTSendMailCorrect(self, 326);
    }
    if (ORBIT(ORQ((char *)s, 1), 9) && ORBIT(ORM((char *)s, 1), 9)) {
        ACTSendMailCorrect(self, 328);
    }
    if (ORBIT(ORQ((char *)s, 1), 18) && ORBIT(ORM((char *)s, 1), 18)) {
        ACTSendMailCorrect(self, 317);
    }
    if (ORBIT(ORQ((char *)s, 1), 19) && ORBIT(ORM((char *)s, 1), 19)) {
        ACTSendMailCorrect(self, 318);
    }
    if (ORBIT(ORQ((char *)s, 1), 20) && ORBIT(ORM((char *)s, 1), 20)) {
        ACTSendMailCorrect(self, 319);
    }
    if (ORBIT(ORQ((char *)s, 1), 22) && ORBIT(ORM((char *)s, 1), 22)) {
        ACTSendMailCorrect(self, 320);
    }
    if (ORBIT(ORQ((char *)s, 1), 23) && ORBIT(ORM((char *)s, 1), 23)) {
        ACTSendMailCorrect(self, 320);
    }
    if (ORBIT(ORQ((char *)s, 1), 27) && ORBIT(ORM((char *)s, 1), 27)) {
        ACTSendMailCorrect(self, 317);
    }
    if (ORBIT(ORQ((char *)s, 1), 28) && ORBIT(ORM((char *)s, 1), 28)) {
        ACTSendMailCorrect(self, 318);
    }
    if (ORBIT(ORQ((char *)s, 1), 29) && ORBIT(ORM((char *)s, 1), 29)) {
        ACTSendMailCorrect(self, 319);
    }
    if (ORBIT(ORQ((char *)s, 1), 55) && ORBIT(ORM((char *)s, 1), 55)) {
        ACTSendMailCorrect(self, 42);
    }
    if (ORBIT(ORQ((char *)s, 1), 56) && ORBIT(ORM((char *)s, 1), 56)) {
        ACTSendMailCorrect(self, 41);
    }
    if (ORBIT(ORQ((char *)s, 1), 57) && ORBIT(ORM((char *)s, 1), 57)) {
        ACTSendMailCorrect(self, 135);
    }
    if (ORBIT(ORQ((char *)s, 1), 58) && ORBIT(ORM((char *)s, 1), 58)) {
        ACTSendMailCorrect(self, 136);
    }
    if (ORBIT(ORQ((char *)s, 1), 63) && ORBIT(ORM((char *)s, 1), 63)) {
        ACTSendMailCorrect(self, 297);
    }
    if (ORBIT(ORQ((char *)s, 2), 0) && ORBIT(ORM((char *)s, 2), 0)) {
        ACTSendMailCorrect(self, 299);
    }
    if ((ORBIT(ORQ((char *)s, 2), 1) && ORBIT(ORM((char *)s, 2), 1)) ||
        (ORBIT(ORQ((char *)s, 2), 2) && ORBIT(ORM((char *)s, 2), 2))) {
        ACTSendMailCorrect(self, 295);
    }
    if (ORBIT(ORQ((char *)s, 1), 53) && ORBIT(ORM((char *)s, 1), 53)) {
        ACTSendMailCorrect(self, 301);
    }
    if (ORBIT(ORQ((char *)s, 1), 54) && ORBIT(ORM((char *)s, 1), 54)) {
        ACTSendMailCorrect(self, 146);
    }
    if (ORBIT(ORQ((char *)s, 1), 45) && ORBIT(ORM((char *)s, 1), 45)) {
        ACTSendMailCorrect(self, 295);
    }
    if (ORBIT(ORQ((char *)s, 1), 46) && ORBIT(ORM((char *)s, 1), 46)) {
        ACTSendMailCorrect(self, 300);
    }
    if (ORBIT(ORQ((char *)s, 1), 47) && ORBIT(ORM((char *)s, 1), 47)) {
        ACTSendMailCorrect(self, 306);
        if (stage_no == 32) {
            ACTSendMailCorrect(self, 308);
        }
    }
    if (ORBIT(ORQ((char *)s, 1), 48) && ORBIT(ORM((char *)s, 1), 48)) {
        ACTSendMailCorrect(self, 307);
    }
    if (ORBIT(ORQ((char *)s, 1), 50) && ORBIT(ORM((char *)s, 1), 50)) {
        ACTSendMailCorrect(self, 310);
    }
    if (ORBIT(ORQ((char *)s, 1), 51) && ORBIT(ORM((char *)s, 1), 51)) {
        ACTSendMailCorrect(self, 311);
    }
    if (ORBIT(ORQ((char *)s, 1), 49) && ORBIT(ORM((char *)s, 1), 49)) {
        ACTSendMailCorrect(self, 309);
    }
    if (ORBIT(ORQ((char *)s, 1), 52) && ORBIT(ORM((char *)s, 1), 52)) {
        ACTSendMailCorrect(self, 312);
    }
    if (ORBIT(ORQ((char *)s, 2), 10) && ORBIT(ORM((char *)s, 2), 10)) {
        ACTSendMailCorrect(self, 90);
    }
    if (ORBIT(ORQ((char *)s, 2), 11) && ORBIT(ORM((char *)s, 2), 11)) {
        ACTSendMailCorrect(self, 91);
    }
    if (ORBIT(ORQ((char *)s, 2), 12) && ORBIT(ORM((char *)s, 2), 12)) {
        (GOBJ_WORK(self)->orientFrames)++;
    } else {
        GOBJ_WORK(self)->orientFrames = 0;
    }
    if (ORBIT(ORQ((char *)s, 2), 7) && ORBIT(ORM((char *)s, 2), 7)) {
        ACTSendMailCorrect(self, 72);
        s->orientMot = 106;
    }
    if (ORBIT(ORQ((char *)s, 2), 8) && ORBIT(ORM((char *)s, 2), 8)) {
        ACTSendMailCorrect(self, 72);
        s->orientMot = 108;
    }
    if (ORBIT(ORQ((char *)s, 2), 9) && ORBIT(ORM((char *)s, 2), 9)) {
        ACTSendMailCorrect(self, 72);
        s->orientMot = 110;
    }
    if (ORBIT(ORQ((char *)s, 1), 24) && ORBIT(ORM((char *)s, 1), 24)) {
        ACTSendMailCorrect(self, 321);
    }
    if (ORBIT(ORQ((char *)s, 1), 25) && ORBIT(ORM((char *)s, 1), 25)) {
        ACTSendMailCorrect(self, 322);
    }
    if (ORBIT(ORQ((char *)s, 1), 26) && ORBIT(ORM((char *)s, 1), 26)) {
        ACTSendMailCorrect(self, 323);
    }
    if (ORBIT(ORQ((char *)s, 1), 62) && ORBIT(ORM((char *)s, 1), 62)) {
        ACTSendMailCorrect(self, 165);
    }
    if (ORBIT(ORQ((char *)s, 2), 13) && ORBIT(ORM((char *)s, 2), 13)) {
        ACTSendMailCorrect(self, 392);
    }
    if (ORBIT(ORQ((char *)s, 2), 14) && ORBIT(ORM((char *)s, 2), 14)) {
        vel = s->env.cliffOrient;
        sceVu0ScaleVector(v0, vel, -sceVu0InnerProduct((char *)GOBJ_SUB(self)->root.move, vel));
        sceVu0AddVector((char *)GOBJ_SUB(self)->root.move, (char *)GOBJ_SUB(self)->root.move, v0);
        SetRootPosition(self, s->env.cliffBackPos);
    }
    if (GOBJ_ACT(self)->enemy->stoneLevel > 0) {
        ACTSendMailCorrect(self, 111);
    }
    if (((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags2 >> 2) & 1) {
        if (GetMotionFrameFlag1(self)) {
            memset(&w1, 0, sizeof(w1));
            GetSkeltonPosition(sk1, self, 0x33);
            GetSkeltonPosition(sk2, self, 0x2F);
            sceVu0AddVector(p1, sk1, sk2);
            sceVu0ScaleVector(p1, p1, 0.5f);
            p1[1] = p1[1] + 50.0f;
            sceVu0ScaleVector(d1, test_CURRENTORIENT(self), 50.0f);
            sceVu0AddVector(w1.pt[0], p1, d1);
            sceVu0ScaleVector(d1, test_CURRENTORIENT(self), -50.0f);
            sceVu0AddVector(w1.pt[1], p1, d1);
            w1.radius = 0.0f;
            ClipWall(&w1);
            if (CompareAttribute(w1.attr, 0x2000)) {
                ACTSendMailCorrect(self, 327);
            }
            if (CompareAttribute(w1.attr, 0x20000)) {
                if (s->addData != 0) {
                    if (GetMotionFrameFlag1(self)) {
                        char *ext;
                        ((MotOriTarget *)s->addData)->wall = w1.wall;
                        ext = (char *)s->addData;
                        ActSendMail_WithAdditionalData(self, 298, self, ext);
                    }
                }
            }
        }
    }
    if (((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags2 >> 3) & 1) {
        memset(&w2, 0, sizeof(w2));
        near = 0;
        p2[0] = test_CURRENTROOT(self)[0];
        p2[1] = test_CURRENTROOT(self)[1];
        p2[2] = test_CURRENTROOT(self)[2];
        sceVu0ScaleVector(d2, test_CURRENTORIENT(self), -50.0f);
        sceVu0AddVector(w2.pt[0], p2, d2);
        sceVu0ScaleVector(d2, test_CURRENTORIENT(self), 50.0f);
        sceVu0AddVector(w2.pt[1], p2, d2);
        hitA = 0;
        w2.radius = 0.0f;
        ClipWall(&w2);
        hitB = 0;
        if (CompareAttribute(w2.attr, 0x400)) {
            hitA = 1;
        }
        if (CompareAttribute(w2.attr, 0xC000)) {
            hitB = 1;
        }
        if (hitA || hitB) {
            GetCollisCenterPositionSimple(c1, w2.wall.o.obj, w2.wall.elem);
            if (_DistxzSqGV(c1, test_CURRENTROOT(self)) < 400.0f) {
                near = 1;
            }
        }
        if (hitA && near) {
            ACTSendMailCorrect(self, 139);
        }
        if (hitB && near) {
            ACTSendMailCorrect(self, 310);
        }
    }
    if (s->intrKind == 313) {
        return;
    }
    for (i = 0; i < 2; i++) {
        if (((int)(s->flags20.ll >> 42) & 1) == 0) {
            continue;
        }
        memset(&w3, 0, sizeof(w3));
        GetSkeltonPosition(sk3, self, 0x33);
        GetSkeltonPosition(sk4, self, 0x2F);
        sceVu0AddVector(c1, sk3, sk4);
        sceVu0ScaleVector(c1, c1, 0.5f);
        c1[1] = c1[1] + 50.0f;
        sceVu0ScaleVector(d3, test_CURRENTORIENT(self), 50.0f);
        _ApplyRyGV(d3, -1.5707964f);
        sceVu0AddVector(w3.pt[0], c1, d3);
        sceVu0ScaleVector(d3, test_CURRENTORIENT(self), 50.0f);
        _ApplyRyGV(d3, 1.5707964f);
        sceVu0AddVector(w3.pt[1], c1, d3);
        if (i == 1) {
            SwapGV(w3.pt[0], w3.pt[1]);
        }
        w3.radius = 0.0f;
        ClipWall(&w3);
        if (w3.wall.elem != 0) {
            GetOrientOfWall(ow, w3.wall.elem, &w3.wall.o);
            SetMotionDirection(self, ow);
            s->flags20.ll &= ~(1ULL << 42);
            return;
        }
    }
}

static void GetGirlHandlinkClInfo(void)
{
    float boyPos[4];
    float girlPos[4];
    float boyHand[4];
    float girlHand[4];
    int attr;
    int ok;
    float dy;

    *(HandClInfo *)&GOBJ_WORK(girlGObj)->handCl = handClInfoClear;
    if (boyGObj == 0 || ((char *)girlGObj) == 0) {
        return;
    }
    GetRootProjectionPosOfGObj(boyPos, boyGObj);
    GetRootProjectionPosOfGObj(girlPos, girlGObj);
    if (ACTGame_FLAG_TETSUNAGI() == 0) {
        if (!(_DistxzSqGV(boyPos, girlPos) < 12100.0f)) {
            goto draw;
        }
        dy = boyPos[1] - girlPos[1];
        if (dy < 0.0f) {
            if (-dy < 60.0f) {
                goto work;
            }
            goto draw;
        }
        if (!(dy < 60.0f)) {
            goto draw;
        }
    }
work:
    ((HandClInfo *)&GOBJ_WORK(girlGObj)->handCl)->on = 1;
    GetSkeltonPosition(boyHand, boyGObj, 44);
    GetSkeltonPosition(girlHand, (girlGObj), 44);

    ok = ACTCheckCollis_W(20.0f, girlHand, boyHand, 0, 0,
                          ((HandClInfo *)&GOBJ_WORK(girlGObj)->handCl)->orient, &attr);
    ((HandClInfo *)&GOBJ_WORK(girlGObj)->handCl)->hit = ok;
    if (CompareAttribute(attr, 0x40000)) {
        ((HandClInfo *)&GOBJ_WORK(girlGObj)->handCl)->attr = 1;
    }
    ok = ACTCheckCollis_W(20.0f, boyHand, girlHand, 0, 0,
                          ((HandClInfo *)&GOBJ_WORK(girlGObj)->handCl)->orient2, &attr);
    ((HandClInfo *)&GOBJ_WORK(girlGObj)->handCl)->hit2 = ok;
    if (CompareAttribute(attr, 0x40000)) {
        ((HandClInfo *)&GOBJ_WORK(girlGObj)->handCl)->attr2 = 1;
    }

draw:
    if (((HandClInfo *)&GOBJ_WORK(girlGObj)->handCl)->hit) {
        debug_Arrow(100.0f, test_CURRENTROOT(girlGObj),
                    ((HandClInfo *)&GOBJ_WORK(girlGObj)->handCl)->orient, 255, 0, 0);
    }
    if (((HandClInfo *)&GOBJ_WORK(girlGObj)->handCl)->hit2) {
        debug_Arrow(100.0f, test_CURRENTROOT(boyGObj),
                    ((HandClInfo *)&GOBJ_WORK(girlGObj)->handCl)->orient2, 0, 0, 255);
    }
}

static int hand_able_connect(void)
{
    unsigned char *h;
    float boy[4];
    float girl[4];

    if (boyGObj == 0 || ((char *)girlGObj) == 0) {
        return 0;
    }
    /* the girl's work + 0x540..0x561 is ActWork.handCl (wider records before
       it on the host) */
    if (debug_font_flag & 1) {
        HandClInfo *d = &GOBJ_WORK(girlGObj)->handCl;

        debug_Printf(10, 120, 0x0FFFFFFF, "[%d] [%d] [%d] [%d] [%d]\n", d->on, d->hit, d->attr,
                     d->hit2, d->attr2);
    }
    {
        HandClInfo *hc = &GOBJ_WORK(girlGObj)->handCl;

        if (hc->on != 0) {
            if (hc->hit != 0 || hc->hit2 != 0) {
                if (hc->attr != 0) {
                    return 1;
                }
                if (hc->attr2 == 0) {
                    return 0;
                }
            }
            return 1;
        }
    }
    (void)h;
    boy[0] = test_CURRENTROOT(boyGObj)[0];
    boy[1] = test_CURRENTROOT(boyGObj)[1];
    boy[2] = test_CURRENTROOT(boyGObj)[2];
    girl[0] = test_CURRENTROOT(girlGObj)[0];
    girl[1] = test_CURRENTROOT(girlGObj)[1];
    girl[2] = test_CURRENTROOT(girlGObj)[2];
    if (GOBJ_ACT(((char *)girlGObj))->actMode != 29 || _DistSqGV((int *)boy, girl) < 10000.0f) {
        return 0;
    }
    return 0;
}

static void ACTGame_CommonLoop_ntsc(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    GObj *gobj;
    unsigned char handL;
    unsigned char handR;
    float third;
    float half;

    if (self == (girlGObj)) {
        GetGirlHandlinkClInfo();
    }

    ACTEnvGetTest(self, (char *)s->dir);

    ActOrientTest(self);

    ACTItemWatchMotion(self);

    if (CheckFloorAttribute(self, 0x7000)) {
        _ACTParaStatus_Set(self, 17);
        _ACTCharStatus_Set(self, 20, 0.0f, 0);
    }
    if (CheckFloorAttribute(self, 0x8000)) {
        _ACTParaStatus_Set(self, 16);
        _ACTCharStatus_Set(self, 19, 0.0f, 0);
    }
    if (CheckFloorAttribute(self, 0x9000)) {
        _ACTParaStatus_Set(self, 15);
        _ACTCharStatus_Set(self, 18, 0.0f, 0);
    }
    if (CheckFloorAttribute(self, 0xA000)) {
        _ACTParaStatus_Set(self, 14);
        _ACTCharStatus_Set(self, 17, 0.0f, 0);
    }

    if (((int)(s->wish1.ll >> 61) & 1) && ((int)(s->wish3.ll >> 61) & 1)) {
        _ACTParaStatus_Set(self, 18);
        _ACTCharStatus_Set(self, 22, 0.0f, 0);
    }

    if ((((int)(s->wish1.ll >> 59) & 1) && ((int)(s->wish3.ll >> 59) & 1)) ||
        (((int)(s->wish1.ll >> 60) & 1) && ((int)(s->wish3.ll >> 60) & 1))) {
        _ACTParaStatus_Set(self, 26);
        _ACTCharStatus_Set(self, 21, 0.0f, 0);
    }

    handL = 0;
    handR = 0;
    if (((int)(s->wish0.ll >> 35) & 1) && ((int)(s->wish2.ll >> 35) & 1)) {
        _ACTParaStatus_Set(self, 24);
        handL = (((&motionKind[ICO_RAW(int, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x4A0,
                                       GOBJ_SUB(self)->ctrl.motion)])
                      ->modeBits.w >>
                  8) &
                 0xF) != 0;
    }
    if (((int)(s->wish0.ll >> 36) & 1) && ((int)(s->wish2.ll >> 36) & 1)) {
        _ACTParaStatus_Set(self, 25);
        handR = (((&motionKind[ICO_RAW(int, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x4A0,
                                       GOBJ_SUB(self)->ctrl.motion)])
                      ->modeBits.w >>
                  12) &
                 0xF) != 0;
    }

    if (handL) {
        RequestChangeHandMode(
            self, 0, 1,
            motionIKEffKind[((&motionKind[GOBJ_SUB(self)->ctrl.motion])->modeBits.w >> 8) & 0xF]
                .mode,
            0, 0, 0);
    } else {
        RequestChangeHandMode(self, 0, 1, 0, 0, 0, 0);
    }
    if (handR) {
        RequestChangeHandMode(
            self, 1, 1,
            motionIKEffKind[((&motionKind[GOBJ_SUB(self)->ctrl.motion])->modeBits.w >> 12) & 0xF]
                .mode,
            0, 0, 0);
    } else {
        RequestChangeHandMode(self, 1, 1, 0, 0, 0, 0);
    }

    if (((int)(s->wish0.ll >> 33) & 1) && ((int)(s->wish2.ll >> 33) & 1)) {
        _ACTCharStatus_Set(self, 23, 0.0f, 0);
    }
    if (((int)(s->wish0.ll >> 34) & 1) && ((int)(s->wish2.ll >> 34) & 1)) {
        _ACTCharStatus_Set(self, 24, 0.0f, 0);
    }

    if ((unsigned int)s->actMode < 4) {
        if (s->actMode != 0) {
            _ACTCharStatus_Set(self, 25, 0.0f, 0);
        }
    }

    if (((char *)girlGObj) != 0 && ((int)(GOBJ_ACT(((char *)girlGObj))->flags18.ll >> 36) & 1)) {
        _ACTCharStatus_Set(self, 13, 0.0f, 0);
    }

    gobj = isysGObjSearchFromObjKindID_begin(20);
    while (gobj != 0) {
        gobj = isysGObjSearchFromObjKindID_next(gobj);
    }

    switch (ACTGame_GetCurrentCallStatus(self)) {
    case 1:
    case 2: {
        float orient[4];
        float target[4];
        float *tgt;
        int limit;
        int connect;

        if (self == boyGObj && s->actMode == 1 &&
            (((unsigned long long)(&motionKind[*(int *)((char *)&GOBJ_SUB(self)->ctrl.motion)])
                  ->flags >>
              7) &
             1)) {
            limit = 100;
            if ((int)(s->flags20.ll >> 13) & 1) {
                tgt = target;
                ScpCallCameraGetTarget(tgt);
                _OrientXZGV(orient, tgt, test_CURRENTROOT(boyGObj));
            } else if (((char *)girlGObj) != 0) {
                tgt = test_CURRENTROOT(girlGObj);
                _OrientXZGV(orient, tgt, test_CURRENTROOT(boyGObj));
            } else {
                GetOtherStageGirlOrient(orient, test_CURRENTROOT(self));
                orient[1] = 0.0f;
                limit = 45;
            }
            if ((int)(s->flags20.ll >> 12) & 1) {
                if (limit < _AbsRotyGV(orient, test_CURRENTORIENT(self))) {
                    SetMotionDirection(self, orient);
                    ResetMotionProgramInterpInfo(self, 35);
                }
            }
        }
        brainAddLevelGirl(20.0f);
        brainSetSpMode();
        if (hand_able_connect()) {
            /* boy first */
            if (ACTGame_CheckHandMotion(boyGObj, girlGObj)) {
                connect = 1;
            } else {
                connect = 0;
            }
            if (GOBJ_ACT(((char *)girlGObj))->actMode == 74) {
                connect = 1;
            }
            if (connect) {
                iosOmSendMail(girlGObj, 60, boyGObj);
            }
        }
        break;
    }
    default:
        if (ACTGame_FLAG_TETSUNAGI() && GOBJ_ACT(((char *)girlGObj))->actMode != 78) {
            brainAddLevelGirl(3.0f);
        }
        break;
    }

    third = (60 - systemStatus[0] * 10) / systemStatus[1] / 3;
    half = (60 - systemStatus[0] * 10) / systemStatus[1] / 2;

    if (GOBJ_ACT(self)->enemy->liftToggle != 0) {
        if (0.5f < s->stick.mag) {
            GOBJ_ACT(self)->enemy->liftToggle = !GOBJ_ACT(self)->enemy->liftToggle;
            (GOBJ_ACT(self)->enemy->liftLevel)--;
            GOBJ_ACT(self)->enemy->liftTimer = third;
        }
    } else {
        if (!(0.5f < s->stick.mag)) {
            GOBJ_ACT(self)->enemy->liftToggle = !GOBJ_ACT(self)->enemy->liftToggle;
        }
    }

    if (0.5f < s->stick.mag) {
        switch (GOBJ_ACT(self)->enemy->liftPhase) {
        case 0:
            if (0.5f < s->stick.dz) {
                GOBJ_ACT(self)->enemy->liftPhase = 1;
                (GOBJ_ACT(self)->enemy->liftLevel)--;
                GOBJ_ACT(self)->enemy->liftTimer = half;
            }
            break;
        case 1:
            if (!(0.5f < s->stick.dz)) {
                GOBJ_ACT(self)->enemy->liftPhase = 0;
                (GOBJ_ACT(self)->enemy->liftLevel)--;
                GOBJ_ACT(self)->enemy->liftTimer = half;
            }
            break;
        case -1:
            if (0.5f < s->stick.dz) {
                GOBJ_ACT(self)->enemy->liftPhase = 1;
            } else {
                GOBJ_ACT(self)->enemy->liftPhase = 0;
            }
            break;
        }
    } else {
        GOBJ_ACT(self)->enemy->liftPhase = -1;
    }

    if (GOBJ_ACT(self)->enemy->liftTimer > 0) {
        GOBJ_ACT(self)->enemy->floorAttrOff = 1;
    } else {
        GOBJ_ACT(self)->enemy->floorAttrOff = 0;
    }
    (GOBJ_ACT(self)->enemy->liftTimer)--;

    if (((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags >> 29) & 1) {
        s->msgBlockTimer = (60 - systemStatus[0] * 10) / systemStatus[1] / 3;
    }

    ACTGame_LwsEffectProcess(self);
    actGame_SendMailToBirds(self);
    ACTGame_SetMotionPlaySpeedRatio_Exec(self);
}

/* PC port: the demo's helpers of ACTGame_CommonLoop (GNU nested functions there): nonzero
   when a wall stands between the two hands, either way round (a wall of attribute 0x40000 does
   not count), and whether the hands can join */
static int girlHandlinkClInfo_jun26(float *a, float *b) /* derived name */
{
    int attr;
    int hit;

    hit = ACTCheckCollis_W(0.0f, a, b, 0, 0, 0, &attr);
    if (CompareAttribute(attr, 0x40000)) {
        return 0;
    }
    if (hit == 0) {
        return 0;
    }
    hit = ACTCheckCollis_W(0.0f, b, a, 0, 0, 0, &attr);
    if (CompareAttribute(attr, 0x40000)) {
        hit = 0;
    }
    return hit;
}

static int hand_able_connect_jun26(void) /* derived name */
{
    float boyPos[4];
    float girlPos[4];
    float boyHand[4];
    float girlHand[4];
    float dy;

    if (boyGObj != 0 && ((char *)girlGObj) != 0) {
        GetRootProjectionPosOfGObj(boyPos, boyGObj);
        GetRootProjectionPosOfGObj(girlPos, girlGObj);
        if (_DistxzSqGV(boyPos, girlPos) < 12100.0f) {
            dy = boyPos[1] - girlPos[1];
            if ((dy < 0.0f ? -dy : dy) < 60.0f) {
                GetSkeltonPosition(boyHand, boyGObj, 2);
                GetSkeltonPosition(girlHand, girlGObj, 18);
                return girlHandlinkClInfo_jun26(boyHand, girlHand) == 0;
            }
        }
    }
    return 0;
}

static void ACTGame_CommonLoop_jun26(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    GObj *gobj;
    unsigned char handL;
    unsigned char handR;
    float third;
    float half;

    ACTEnvGetTest(self, (char *)s->dir);

    ActOrientTest(self);

    ACTItemWatchMotion(self);

    if (CheckFloorAttribute(self, 0x7000)) {
        _ACTParaStatus_Set(self, 19);
        _ACTCharStatus_Set(self, 18, 0.0f, 0);
    }
    if (CheckFloorAttribute(self, 0x8000)) {
        _ACTParaStatus_Set(self, 18);
        _ACTCharStatus_Set(self, 17, 0.0f, 0);
    }
    if (CheckFloorAttribute(self, 0x9000)) {
        _ACTParaStatus_Set(self, 17);
        _ACTCharStatus_Set(self, 16, 0.0f, 0);
    }
    if (CheckFloorAttribute(self, 0xA000)) {
        _ACTParaStatus_Set(self, 16);
        _ACTCharStatus_Set(self, 15, 0.0f, 0);
    }

    if (((int)(s->wish1.ll >> 54) & 1) && ((int)(s->wish3.ll >> 22) & 1)) {
        _ACTParaStatus_Set(self, 21);
        _ACTCharStatus_Set(self, 20, 0.0f, 0);
    }

    if ((((int)(s->wish1.ll >> 52) & 1) && ((int)(s->wish3.ll >> 20) & 1)) ||
        (((int)(s->wish1.ll >> 53) & 1) && ((int)(s->wish3.ll >> 21) & 1))) {
        _ACTParaStatus_Set(self, 20);
        _ACTCharStatus_Set(self, 19, 0.0f, 0);
    }

    handL = 0;
    handR = 0;
    if (((int)(s->wish0.ll >> 35) & 1) && ((int)(s->wish2.ll >> 3) & 1)) {
        _ACTParaStatus_Set(self, 5);
        handL = (((&motionKind[ICO_RAW(int, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x4A0,
                                       GOBJ_SUB(self)->ctrl.motion)])
                      ->modeBits.w >>
                  12) &
                 0xF) != 0;
    }
    if (((int)(s->wish0.ll >> 36) & 1) && ((int)(s->wish2.ll >> 4) & 1)) {
        _ACTParaStatus_Set(self, 6);
        handR = (((&motionKind[ICO_RAW(int, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x4A0,
                                       GOBJ_SUB(self)->ctrl.motion)])
                      ->modeBits.w >>
                  8) &
                 0xF) != 0;
    }

    if (handL) {
        RequestChangeHandMode(
            self, 0, 1,
            motionIKEffKind[((&motionKind[GOBJ_SUB(self)->ctrl.motion])->modeBits.w >> 12) & 0xF]
                .mode,
            0, 0, 0);
    } else {
        RequestChangeHandMode(self, 0, 1, 0, 0, 0, 0);
    }
    if (handR) {
        RequestChangeHandMode(
            self, 1, 1,
            motionIKEffKind[((&motionKind[GOBJ_SUB(self)->ctrl.motion])->modeBits.w >> 8) & 0xF]
                .mode,
            0, 0, 0);
    } else {
        RequestChangeHandMode(self, 1, 1, 0, 0, 0, 0);
    }

    if (((int)(s->wish0.ll >> 33) & 1) && ((int)(s->wish2.ll >> 1) & 1)) {
        _ACTCharStatus_Set(self, 21, 0.0f, 0);
    }
    if (((int)(s->wish0.ll >> 34) & 1) && ((int)(s->wish2.ll >> 2) & 1)) {
        _ACTCharStatus_Set(self, 22, 0.0f, 0);
    }

    if ((unsigned int)s->actMode < 4) {
        if (s->actMode != 0) {
            _ACTCharStatus_Set(self, 23, 0.0f, 0);
        }
    }

    if (((char *)girlGObj) != 0 && ((int)(GOBJ_ACT(((char *)girlGObj))->flags18.ll >> 35) & 1)) {
        _ACTCharStatus_Set(self, 11, 0.0f, 0);
    }

    gobj = isysGObjSearchFromObjKindID_begin(20);
    while (gobj != 0) {
        gobj = isysGObjSearchFromObjKindID_next(gobj);
    }

    switch (ACTGame_GetCurrentCallStatus(self)) {
    case 1:
    case 2: {
        float orient[4];
        int limit;

        if (self == boyGObj && s->actMode == 1 &&
            (((&motionKind[*(int *)((char *)&GOBJ_SUB(self)->ctrl.motion)])->flags >> 4) & 1)) {
            limit = 100;
            if (((char *)girlGObj) != 0) {
                _OrientXZGV(orient, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
            } else {
                GetOtherStageGirlOrient(orient, GetCurrentCameraSet2());
                orient[1] = 0.0f;
                limit = 45;
            }
            if (limit < _AbsRotyGV(orient, test_CURRENTORIENT(self))) {
                SetMotionDirection(self, orient);
                ResetMotionProgramInterpInfo(self, 35);
            }
        }
        brainAddLevelGirl(20.0f);
        brainSetSpMode();
        if (hand_able_connect_jun26()) {
            iosOmSendMail(girlGObj, 58, boyGObj);
        }
        break;
    }
    default:
        if (ACTGame_FLAG_TETSUNAGI() && GOBJ_ACT(((char *)girlGObj))->actMode != 77) {
            brainAddLevelGirl(3.0f);
        }
        break;
    }

    third = (60 - systemStatus[0] * 10) / systemStatus[1] / 3;
    half = (60 - systemStatus[0] * 10) / systemStatus[1] / 2;

    if (GOBJ_ACT(self)->enemy->liftToggle != 0) {
        if (0.5f < s->stick.mag) {
            GOBJ_ACT(self)->enemy->liftToggle = !GOBJ_ACT(self)->enemy->liftToggle;
            (GOBJ_ACT(self)->enemy->liftLevel)--;
            GOBJ_ACT(self)->enemy->liftTimer = third;
        }
    } else {
        if (!(0.5f < s->stick.mag)) {
            GOBJ_ACT(self)->enemy->liftToggle = !GOBJ_ACT(self)->enemy->liftToggle;
        }
    }

    if (0.5f < s->stick.mag) {
        switch (GOBJ_ACT(self)->enemy->liftPhase) {
        case 0:
            if (0.5f < s->stick.dz) {
                GOBJ_ACT(self)->enemy->liftPhase = 1;
                (GOBJ_ACT(self)->enemy->liftLevel)--;
                GOBJ_ACT(self)->enemy->liftTimer = half;
            }
            break;
        case 1:
            if (!(0.5f < s->stick.dz)) {
                GOBJ_ACT(self)->enemy->liftPhase = 0;
                (GOBJ_ACT(self)->enemy->liftLevel)--;
                GOBJ_ACT(self)->enemy->liftTimer = half;
            }
            break;
        case -1:
            if (0.5f < s->stick.dz) {
                GOBJ_ACT(self)->enemy->liftPhase = 1;
            } else {
                GOBJ_ACT(self)->enemy->liftPhase = 0;
            }
            break;
        }
    } else {
        GOBJ_ACT(self)->enemy->liftPhase = -1;
    }

    if (GOBJ_ACT(self)->enemy->liftTimer > 0) {
        GOBJ_ACT(self)->enemy->floorAttrOff = 1;
    } else {
        GOBJ_ACT(self)->enemy->floorAttrOff = 0;
    }
    (GOBJ_ACT(self)->enemy->liftTimer)--;

    if (((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags >> 13) & 1) {
        s->msgBlockTimer = (60 - systemStatus[0] * 10) / systemStatus[1] / 3;
    }

    ACTGame_LwsEffectProcess(self);
    actGame_SendMailToBirds(self);
    ACTGame_SetMotionPlaySpeedRatio_Exec(self);
}

static void ACTGame_CommonLoop_aug6(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    GObj *gobj;
    unsigned char handL;
    unsigned char handR;
    float third;
    float half;

    if (self == (girlGObj)) {
        GetGirlHandlinkClInfo();
    }

    ACTEnvGetTest(self, (char *)s->dir);

    ActOrientTest(self);

    ACTItemWatchMotion(self);

    if (CheckFloorAttribute(self, 0x7000)) {
        _ACTParaStatus_Set(self, 17);
        _ACTCharStatus_Set(self, 20, 0.0f, 0);
    }
    if (CheckFloorAttribute(self, 0x8000)) {
        _ACTParaStatus_Set(self, 16);
        _ACTCharStatus_Set(self, 19, 0.0f, 0);
    }
    if (CheckFloorAttribute(self, 0x9000)) {
        _ACTParaStatus_Set(self, 15);
        _ACTCharStatus_Set(self, 18, 0.0f, 0);
    }
    if (CheckFloorAttribute(self, 0xA000)) {
        _ACTParaStatus_Set(self, 14);
        _ACTCharStatus_Set(self, 17, 0.0f, 0);
    }

    if (((int)(s->wish1.ll >> 60) & 1) && ((int)(s->wish3.ll >> 60) & 1)) {
        _ACTParaStatus_Set(self, 18);
        _ACTCharStatus_Set(self, 22, 0.0f, 0);
    }

    if ((((int)(s->wish1.ll >> 58) & 1) && ((int)(s->wish3.ll >> 58) & 1)) ||
        (((int)(s->wish1.ll >> 59) & 1) && ((int)(s->wish3.ll >> 59) & 1))) {
        _ACTParaStatus_Set(self, 26);
        _ACTCharStatus_Set(self, 21, 0.0f, 0);
    }

    handL = 0;
    handR = 0;
    if (((int)(s->wish0.ll >> 35) & 1) && ((int)(s->wish2.ll >> 35) & 1)) {
        _ACTParaStatus_Set(self, 24);
        handL = (((&motionKind[ICO_RAW(int, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x4A0,
                                       GOBJ_SUB(self)->ctrl.motion)])
                      ->modeBits.w >>
                  8) &
                 0xF) != 0;
    }
    if (((int)(s->wish0.ll >> 36) & 1) && ((int)(s->wish2.ll >> 36) & 1)) {
        _ACTParaStatus_Set(self, 25);
        handR = (((&motionKind[ICO_RAW(int, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x4A0,
                                       GOBJ_SUB(self)->ctrl.motion)])
                      ->modeBits.w >>
                  12) &
                 0xF) != 0;
    }

    if (handL) {
        RequestChangeHandMode(
            self, 0, 1,
            motionIKEffKind[((&motionKind[GOBJ_SUB(self)->ctrl.motion])->modeBits.w >> 8) & 0xF]
                .mode,
            0, 0, 0);
    } else {
        RequestChangeHandMode(self, 0, 1, 0, 0, 0, 0);
    }
    if (handR) {
        RequestChangeHandMode(
            self, 1, 1,
            motionIKEffKind[((&motionKind[GOBJ_SUB(self)->ctrl.motion])->modeBits.w >> 12) & 0xF]
                .mode,
            0, 0, 0);
    } else {
        RequestChangeHandMode(self, 1, 1, 0, 0, 0, 0);
    }

    if (((int)(s->wish0.ll >> 33) & 1) && ((int)(s->wish2.ll >> 33) & 1)) {
        _ACTCharStatus_Set(self, 23, 0.0f, 0);
    }
    if (((int)(s->wish0.ll >> 34) & 1) && ((int)(s->wish2.ll >> 34) & 1)) {
        _ACTCharStatus_Set(self, 24, 0.0f, 0);
    }

    if ((unsigned int)s->actMode < 4) {
        if (s->actMode != 0) {
            _ACTCharStatus_Set(self, 25, 0.0f, 0);
        }
    }

    if (((char *)girlGObj) != 0 && ((int)(GOBJ_ACT(((char *)girlGObj))->flags18.ll >> 36) & 1)) {
        _ACTCharStatus_Set(self, 13, 0.0f, 0);
    }

    gobj = isysGObjSearchFromObjKindID_begin(20);
    while (gobj != 0) {
        gobj = isysGObjSearchFromObjKindID_next(gobj);
    }

    switch (ACTGame_GetCurrentCallStatus(self)) {
    case 1:
    case 2: {
        float orient[4];
        float target[4];
        float *tgt;
        int limit;

        if (self == boyGObj && s->actMode == 1 &&
            (((unsigned long long)(&motionKind[*(int *)((char *)&GOBJ_SUB(self)->ctrl.motion)])
                  ->flags >>
              7) &
             1)) {
            limit = 100;
            if ((int)(s->flags20.ll >> 10) & 1) {
                tgt = target;
                ScpCallCameraGetTarget(tgt);
                _OrientXZGV(orient, tgt, test_CURRENTROOT(boyGObj));
            } else if (((char *)girlGObj) != 0) {
                tgt = test_CURRENTROOT(girlGObj);
                _OrientXZGV(orient, tgt, test_CURRENTROOT(boyGObj));
            } else {
                GetOtherStageGirlOrient(orient, GetCurrentCameraSet2());
                orient[1] = 0.0f;
                limit = 45;
            }
            if ((int)(s->flags20.ll >> 9) & 1) {
                if (limit < _AbsRotyGV(orient, test_CURRENTORIENT(self))) {
                    SetMotionDirection(self, orient);
                    ResetMotionProgramInterpInfo(self, 35);
                }
            }
        }
        brainAddLevelGirl(20.0f);
        brainSetSpMode();
        if (hand_able_connect()) {
            iosOmSendMail(girlGObj, 59, boyGObj);
        }
        break;
    }
    default:
        if (ACTGame_FLAG_TETSUNAGI() && GOBJ_ACT(((char *)girlGObj))->actMode != 78) {
            brainAddLevelGirl(3.0f);
        }
        break;
    }

    third = (60 - systemStatus[0] * 10) / systemStatus[1] / 3;
    half = (60 - systemStatus[0] * 10) / systemStatus[1] / 2;

    if (GOBJ_ACT(self)->enemy->liftToggle != 0) {
        if (0.5f < s->stick.mag) {
            GOBJ_ACT(self)->enemy->liftToggle = !GOBJ_ACT(self)->enemy->liftToggle;
            (GOBJ_ACT(self)->enemy->liftLevel)--;
            GOBJ_ACT(self)->enemy->liftTimer = third;
        }
    } else {
        if (!(0.5f < s->stick.mag)) {
            GOBJ_ACT(self)->enemy->liftToggle = !GOBJ_ACT(self)->enemy->liftToggle;
        }
    }

    if (0.5f < s->stick.mag) {
        switch (GOBJ_ACT(self)->enemy->liftPhase) {
        case 0:
            if (0.5f < s->stick.dz) {
                GOBJ_ACT(self)->enemy->liftPhase = 1;
                (GOBJ_ACT(self)->enemy->liftLevel)--;
                GOBJ_ACT(self)->enemy->liftTimer = half;
            }
            break;
        case 1:
            if (!(0.5f < s->stick.dz)) {
                GOBJ_ACT(self)->enemy->liftPhase = 0;
                (GOBJ_ACT(self)->enemy->liftLevel)--;
                GOBJ_ACT(self)->enemy->liftTimer = half;
            }
            break;
        case -1:
            if (0.5f < s->stick.dz) {
                GOBJ_ACT(self)->enemy->liftPhase = 1;
            } else {
                GOBJ_ACT(self)->enemy->liftPhase = 0;
            }
            break;
        }
    } else {
        GOBJ_ACT(self)->enemy->liftPhase = -1;
    }

    if (GOBJ_ACT(self)->enemy->liftTimer > 0) {
        GOBJ_ACT(self)->enemy->floorAttrOff = 1;
    } else {
        GOBJ_ACT(self)->enemy->floorAttrOff = 0;
    }
    (GOBJ_ACT(self)->enemy->liftTimer)--;

    if (((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags >> 29) & 1) {
        s->msgBlockTimer = (60 - systemStatus[0] * 10) / systemStatus[1] / 3;
    }

    ACTGame_LwsEffectProcess(self);
    actGame_SendMailToBirds(self);
    ACTGame_SetMotionPlaySpeedRatio_Exec(self);
}

void ACTGame_CommonLoop(GObj *self)
{
    if (ICO_JUN26) {
        ACTGame_CommonLoop_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        ACTGame_CommonLoop_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        ACTGame_CommonLoop_aug6(self);
        return;
    }
    Act *s = GOBJ_ACT(self);
    GObj *gobj;
    unsigned char handL;
    unsigned char handR;
    float third;
    float half;

    if (self == (girlGObj)) {
        GetGirlHandlinkClInfo();
    }

    ACTEnvGetTest(self, (char *)s->dir);

    ActOrientTest(self);

    ACTItemWatchMotion(self);

    if (CheckFloorAttribute(self, 0x7000)) {
        _ACTParaStatus_Set(self, 17);
        _ACTCharStatus_Set(self, 22, 0.0f, 0);
    }
    if (CheckFloorAttribute(self, 0x8000)) {
        _ACTParaStatus_Set(self, 16);
        _ACTCharStatus_Set(self, 21, 0.0f, 0);
    }
    if (CheckFloorAttribute(self, 0x9000)) {
        _ACTParaStatus_Set(self, 15);
        _ACTCharStatus_Set(self, 20, 0.0f, 0);
    }
    if (CheckFloorAttribute(self, 0xA000)) {
        _ACTParaStatus_Set(self, 14);
        _ACTCharStatus_Set(self, 19, 0.0f, 0);
    }

    if (((int)(s->wish2.ll >> 5) & 1) && ((int)(s->wish4.ll >> 5) & 1)) {
        _ACTParaStatus_Set(self, 18);
        _ACTCharStatus_Set(self, 24, 0.0f, 0);
    }

    if ((((int)(s->wish2.ll >> 3) & 1) && ((int)(s->wish4.ll >> 3) & 1)) ||
        (((int)(s->wish2.ll >> 4) & 1) && ((int)(s->wish4.ll >> 4) & 1))) {
        _ACTParaStatus_Set(self, 26);
        _ACTCharStatus_Set(self, 23, 0.0f, 0);
    }

    if (girlControlMode != 0 && ((int)(s->wish0.ll >> 33) & 1) && ((int)(s->wish2.ll >> 33) & 1)) {
        _ACTParaStatus_Set(self, 27);
    }

    handL = 0;
    handR = 0;
    if (((int)(s->wish0.ll >> 36) & 1) && ((int)(s->wish2.ll >> 36) & 1)) {
        _ACTParaStatus_Set(self, 24);
        handL = (((&motionKind[ICO_RAW(int, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x4A0,
                                       GOBJ_SUB(self)->ctrl.motion)])
                      ->modeBits.w >>
                  12) &
                 0xF) != 0;
    }
    if (((int)(s->wish0.ll >> 37) & 1) && ((int)(s->wish2.ll >> 37) & 1)) {
        _ACTParaStatus_Set(self, 25);
        handR = (((&motionKind[ICO_RAW(int, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x4A0,
                                       GOBJ_SUB(self)->ctrl.motion)])
                      ->modeBits.w >>
                  8) &
                 0xF) != 0;
    }

    if (handL) {
        RequestChangeHandMode(
            self, 0, 1,
            motionIKEffKind[((&motionKind[GOBJ_SUB(self)->ctrl.motion])->modeBits.w >> 12) & 0xF]
                .mode,
            0, 0, 0);
    } else {
        RequestChangeHandMode(self, 0, 1, 0, 0, 0, 0);
    }
    if (handR) {
        RequestChangeHandMode(
            self, 1, 1,
            motionIKEffKind[((&motionKind[GOBJ_SUB(self)->ctrl.motion])->modeBits.w >> 8) & 0xF]
                .mode,
            0, 0, 0);
    } else {
        RequestChangeHandMode(self, 1, 1, 0, 0, 0, 0);
    }

    if (((int)(s->wish0.ll >> 34) & 1) && ((int)(s->wish2.ll >> 34) & 1)) {
        _ACTCharStatus_Set(self, 25, 0.0f, 0);
    }
    if (((int)(s->wish0.ll >> 35) & 1) && ((int)(s->wish2.ll >> 35) & 1)) {
        _ACTCharStatus_Set(self, 26, 0.0f, 0);
    }

    if ((unsigned int)s->actMode < 4) {
        if (s->actMode != 0) {
            _ACTCharStatus_Set(self, 27, 0.0f, 0);
        }
    }

    if (((char *)girlGObj) != 0 && ((int)(GOBJ_ACT(((char *)girlGObj))->flags18.ll >> 40) & 1)) {
        _ACTCharStatus_Set(self, 15, 0.0f, 0);
    }

    gobj = isysGObjSearchFromObjKindID_begin(20);
    while (gobj != 0) {
        gobj = isysGObjSearchFromObjKindID_next(gobj);
    }

    switch (ACTGame_GetCurrentCallStatus(self)) {
    case 1:
    case 2: {
        float orient[4];
        float target[4];
        float *tgt;
        int limit;
        int connect;

        if (self == boyGObj && s->actMode == 1 &&
            (((unsigned long long)(&motionKind[*(int *)((char *)&GOBJ_SUB(self)->ctrl.motion)])
                  ->flags2 >>
              7) &
             1)) {
            limit = 100;
            if ((int)(s->flags20.ll >> 24) & 3) {
                tgt = target;
                ScpCallCameraGetTarget(tgt);
                _OrientXZGV(orient, tgt, test_CURRENTROOT(boyGObj));
            } else if (((char *)girlGObj) != 0) {
                tgt = test_CURRENTROOT(girlGObj);
                _OrientXZGV(orient, tgt, test_CURRENTROOT(boyGObj));
            } else {
                GetOtherStageGirlOrient(orient, test_CURRENTROOT(self));
                orient[1] = 0.0f;
                limit = 45;
            }
            if (((int)(s->flags20.ll >> 23) & 1) && !((int)(s->wish1.ll >> 2) & 1)) {
                if (limit < _AbsRotyGV(orient, test_CURRENTORIENT(self))) {
                    SetMotionDirection(self, orient);
                    ResetMotionProgramInterpInfo(self, 35);
                }
            }
        }
        brainAddLevelGirlDetail(1, 20.0f);
        brainSetSpMode();
        if (hand_able_connect()) {
            /* boy first */
            if (ACTGame_CheckHandMotion(boyGObj, girlGObj)) {
                connect = 1;
            } else {
                connect = 0;
            }
            if (GOBJ_ACT(((char *)girlGObj))->actMode == 74) {
                connect = 1;
            }
            if (connect && (s->pad.now & 8)) {
                iosOmSendMail(girlGObj, 63, boyGObj);
            }
        }
        break;
    }
    default:
        if (ACTGame_FLAG_TETSUNAGI() && GOBJ_ACT(((char *)girlGObj))->actMode != 81) {
            brainAddLevelGirl(3.0f);
        }
        break;
    }

    if ((int)(s->flags18.ll >> 58) & 1) {
        if (ACTGame_FLAG_TETSUNAGI() && hand_able_connect() == 0) {
            ACTSendMailCorrect(self, 62);
        }
    }

    third = (60 - systemStatus[0] * 10) / systemStatus[1] / 3;
    half = (60 - systemStatus[0] * 10) / systemStatus[1] / 2;

    if ((s->pad.trg & 0xF0) != 0) {
        (GOBJ_ACT(self)->enemy->liftLevel)--;
    }

    if (GOBJ_ACT(self)->enemy->liftToggle != 0) {
        if (0.5f < s->stick.mag) {
            GOBJ_ACT(self)->enemy->liftToggle = !GOBJ_ACT(self)->enemy->liftToggle;
            (GOBJ_ACT(self)->enemy->liftLevel)--;
            GOBJ_ACT(self)->enemy->liftTimer = third;
        }
    } else {
        if (!(0.5f < s->stick.mag)) {
            GOBJ_ACT(self)->enemy->liftToggle = !GOBJ_ACT(self)->enemy->liftToggle;
        }
    }

    if (0.5f < s->stick.mag) {
        switch (GOBJ_ACT(self)->enemy->liftPhase) {
        case 0:
            if (0.5f < s->stick.dz) {
                GOBJ_ACT(self)->enemy->liftPhase = 1;
                (GOBJ_ACT(self)->enemy->liftLevel)--;
                GOBJ_ACT(self)->enemy->liftTimer = half;
            }
            break;
        case 1:
            if (!(0.5f < s->stick.dz)) {
                GOBJ_ACT(self)->enemy->liftPhase = 0;
                (GOBJ_ACT(self)->enemy->liftLevel)--;
                GOBJ_ACT(self)->enemy->liftTimer = half;
            }
            break;
        case -1:
            if (0.5f < s->stick.dz) {
                GOBJ_ACT(self)->enemy->liftPhase = 1;
            } else {
                GOBJ_ACT(self)->enemy->liftPhase = 0;
            }
            break;
        }
    } else {
        GOBJ_ACT(self)->enemy->liftPhase = -1;
    }

    if (GOBJ_ACT(self)->enemy->liftTimer > 0) {
        GOBJ_ACT(self)->enemy->floorAttrOff = 1;
    } else {
        GOBJ_ACT(self)->enemy->floorAttrOff = 0;
    }
    (GOBJ_ACT(self)->enemy->liftTimer)--;

    if ((&motionKind[GOBJ_SUB(self)->ctrl.motion])->flags2 & 1) {
        s->msgBlockTimer = (60 - systemStatus[0] * 10) / systemStatus[1] / 3;
    }

    ACTGame_LwsEffectProcess(self);
    actGame_SendMailToBirds(self);
    ACTGame_SetMotionPlaySpeedRatio_Exec(self);
}

inline void GetGirlPositionAtThisStage(float *pos)
{
    float buf[4];
    int id = gamesysGetGirlStageIDAndPosition(buf);
    OtherStagePositionGet(pos, stage_no, id, buf);
}

static void GetOtherStageGirlOrient_aug6jun26(float *orient, float *root)
{
    (void)root; /* PC port: the European parameter, unused here */
    StageOrientGet((VECTOR *)orient, stage_no, gamesysGirlStageGet());
}

inline void GetOtherStageGirlOrient(float *orient, float *root)
{
    if (ICO_AUG6JUN26) {
        GetOtherStageGirlOrient_aug6jun26(orient, root);
        return;
    }
    float pos[4];
    GetGirlPositionAtThisStage(pos);
    _OrientGV(orient, pos, root);
}

static inline void GetGirlPositionAheadOfHead(float *pos, GObj *obj);

/* the USA disc's and the August 2001 preview's GetTarget (a GNU nested function of
   ACTLookTargetSystem_Exec there): the girl's look bits are other bits of flags20, the
   preview looks ahead of the head when the girl is on another stage and has no camera
   target, and neither has the hint target (kind 14) */
static int GetTarget_ntscaug6jun26(GObj *self, char *s, int kind, float *pos, int *pmode)
{
    float dir[4];
    float p[4];
    GObj *target = 0;
    int rv = 0;

    switch (kind) {
    case 13:
        GetSkeltonPosition(p, self, 0x23);
        sceVu0ScaleVector(dir, (char *)&GOBJ_WORK(self)->pinchPosX, 300.0f);
        sceVu0AddVector(pos, p, dir);
        GOBJ_SUB(self)->root.ikRate0 = 0.3f;
        GOBJ_SUB(self)->root.ikRate1 = 0.3f;
        GOBJ_SUB(self)->root.ikRate2 = 0.3f;
        rv = 1;
        break;
    case 12:
        target = ((Act *)s)->statusObj;
        break;
    case 10:
        target = ((Act *)s)->statusTarget;
        break;
    case 5:
        target = ((Act *)s)->statusOther;
        break;
    case 4:
        target = girlGObj;
        if (target == 0 && (ICO_JUN26 || (((Act *)s)->flags20.ll & PAL_AUG6JUN26(0x3000, 0x600)) ==
                                             PAL_AUG6JUN26(0x1000, 0x200))) {
            if (ICO_AUG6JUN26) {
                GetGirlPositionAheadOfHead(pos, self);
            } else {
                GetGirlPositionAtThisStage(pos);
            }
            rv = 1;
        }
        if (ICO_NTSC && ((int)(((Act *)s)->flags20.ll >> 13) & 1)) {
            ScpCallCameraGetTarget(pos);
            target = 0;
            rv = 1;
        }
        break;
    case 11:
        GetRootPosition(pos, ICO_RAW(GObj *, s, 0x74, (GObj *)((Act *)s)->statusVal18));
        pos[1] = *(float *)&test_CURRENTROOT(self)[1];
        *pmode = 2;
        rv = 1;
        break;
    case 6:
        target = girlGObj;
        *pmode = 2;
        if (target == 0) {
            if (ICO_AUG6JUN26) {
                GetGirlPositionAheadOfHead(pos, self);
            } else {
                GetGirlPositionAtThisStage(pos);
            }
            rv = 1;
        }
        if (ICO_NTSC && ((int)(((Act *)s)->flags20.ll >> 13) & 1)) {
            ScpCallCameraGetTarget(pos);
            target = 0;
            rv = 1;
        }
        break;
    case 8:
        if (((Act *)s)->frame % 15 / 10 != 0) {
            target = ((Act *)s)->gobj80;
        } else {
            target = boyGObj;
        }
        break;
    case 9:
        target = ((Act *)s)->gobj84;
        break;
    case 7:
        target = boyGObj;
        if (((Act *)s)->frame % 15 / 10 != 0) {
            target = ((Act *)s)->gobj80;
        }
        break;
    case 3:
        target = boyGObj;
        break;
    case 1:
        sceVu0ScaleVector(pos, test_CURRENTORIENT(self), 200.0f);
        pos[1] = 0.0f;
        sceVu0AddVector(pos, test_CURRENTROOT(self), pos);
        rv = 1;
        break;
    case 2:
        sceVu0ScaleVector(pos, test_CURRENTORIENT(self), ((Act *)s)->env.cliffHeight);
        pos[1] = 150.0f;
        sceVu0AddVector(pos, test_CURRENTROOT(self), pos);
        rv = 1;
        break;
    }
    if (target != 0) {
        if (target == boyGObj) {
            /* these two statements written out instead of calling
               GetSkeltonPosition: the node comes off `target` and the
               skeleton off the global */
            int idx = GetSkeltonFocusNode(target, 35) << 6;
            ((IntFloat *)pos)[0].f = *(float *)(idx + GOBJ_SUB(boyGObj)->nodeMtx + 0x30);
            ((IntFloat *)pos)[1].f = *(float *)(idx + GOBJ_SUB(boyGObj)->nodeMtx + 0x34);
            ((IntFloat *)pos)[2].f = *(float *)(idx + GOBJ_SUB(boyGObj)->nodeMtx + 0x38);
        } else {
            GetRootPosition(pos, target);
        }
        rv = 1;
    }
    return rv;
}

static int GetTarget(GObj *self, char *s, int kind, float *pos, int *pmode)
{
    float dir[4];
    float p[4];
    GObj *target = 0;
    int rv = 0;

    if (ICO_NTSCAUG6JUN26) {
        return GetTarget_ntscaug6jun26(self, s, kind, pos, pmode);
    }

    switch (kind) {
    case 13:
        GetSkeltonPosition(p, self, 0x23);
        sceVu0ScaleVector(dir, (char *)&GOBJ_WORK(self)->pinchPosX, 300.0f);
        sceVu0AddVector(pos, p, dir);
        GOBJ_SUB(self)->root.ikRate0 = 0.3f;
        GOBJ_SUB(self)->root.ikRate1 = 0.3f;
        GOBJ_SUB(self)->root.ikRate2 = 0.3f;
        rv = 1;
        break;
    case 12:
        target = ((Act *)s)->statusObj;
        break;
    case 10:
        target = ((Act *)s)->statusTarget;
        break;
    case 5:
        target = ((Act *)s)->statusOther;
        break;
    case 4:
        target = girlGObj;
        if (target == 0 && (((Act *)s)->flags20.ll & 0x3800000) == 0x800000) {
            GetGirlPositionAtThisStage(pos);
            rv = 1;
        }
        if ((int)(((Act *)s)->flags20.ll >> 24) & 3) {
            ScpCallCameraGetTarget(pos);
            target = 0;
            rv = 1;
        }
        break;
    case 11:
        GetRootPosition(pos, ICO_RAW(GObj *, s, 0x74, (GObj *)((Act *)s)->statusVal18));
        pos[1] = *(float *)&test_CURRENTROOT(self)[1];
        *pmode = 2;
        rv = 1;
        break;
    case 6:
        if ((int)(ICO_RAW(unsigned long long, s, 0x20, ((Act *)s)->flags20.ll) >> 23) & 1) {
            target = girlGObj;
            *pmode = 2;
            if (target == 0) {
                GetGirlPositionAtThisStage(pos);
                rv = 1;
            }
        }
        if ((int)(((Act *)s)->flags20.ll >> 24) & 3) {
            ScpCallCameraGetTarget(pos);
            target = 0;
            rv = 1;
        }
        break;
    case 8:
        if (((Act *)s)->frame % 15 / 10 != 0) {
            target = ((Act *)s)->gobj80;
        } else {
            target = boyGObj;
        }
        break;
    case 9:
        target = ((Act *)s)->gobj84;
        break;
    case 7:
        target = boyGObj;
        if (((Act *)s)->frame % 15 / 10 != 0) {
            target = ((Act *)s)->gobj80;
        }
        break;
    case 3:
        target = boyGObj;
        break;
    case 1:
        sceVu0ScaleVector(pos, test_CURRENTORIENT(self), 200.0f);
        pos[1] = 0.0f;
        sceVu0AddVector(pos, test_CURRENTROOT(self), pos);
        rv = 1;
        break;
    case 2:
        sceVu0ScaleVector(pos, test_CURRENTORIENT(self), ((Act *)s)->env.cliffHeight);
        pos[1] = 150.0f;
        sceVu0AddVector(pos, test_CURRENTROOT(self), pos);
        rv = 1;
        break;
    case 14:
        pos[0] = GOBJ_WORK(self)->hintPosX;
        pos[1] = GOBJ_WORK(self)->hintPosY;
        pos[2] = GOBJ_WORK(self)->hintPosZ;
        rv = 1;
        break;
    }
    if (target != 0) {
        if (target == boyGObj) {
            /* these two statements written out instead of calling
               GetSkeltonPosition: the node comes off `target` and the
               skeleton off the global */
            int idx = GetSkeltonFocusNode(target, 35) << 6;
            ((IntFloat *)pos)[0].f = *(float *)(idx + GOBJ_SUB(boyGObj)->nodeMtx + 0x30);
            ((IntFloat *)pos)[1].f = *(float *)(idx + GOBJ_SUB(boyGObj)->nodeMtx + 0x34);
            ((IntFloat *)pos)[2].f = *(float *)(idx + GOBJ_SUB(boyGObj)->nodeMtx + 0x38);
        } else {
            GetRootPosition(pos, target);
        }
        rv = 1;
    }
    return rv;
}

static void ACTLookTargetSystem_Exec_ntsc(GObj *self)
{
    char *s = (char *)GOBJ_ACT(self);

    float pos[4];
    int mode = 1;
    int found = 0;
    int col = ((Act *)s)->actKind;
    int flags;
    int i;

    flags = 0;
    if (_ACTCharStatus_Check(self, 29)) {
        flags = 1;
    }
    if (_ACTCharStatus_Check(self, 26)) {
        if (GOBJ_WORK(self)->boyDist < 300.0f) {
            flags |= 0x10;
        } else {
            flags |= 0x200;
        }
    }
    if (_ACTCharStatus_Check(self, 10)) {
        flags |= 0x200000;
    }
    if (_ACTCharStatus_Check(self, 11)) {
        flags |= 0x400;
    }
    if (_ACTCharStatus_Check(self, 13)) {
        if (self == (girlGObj)) {
            if ((int)(((Act *)s)->flags20.ll >> 4) & 1) {
                flags |= 0x80;
            }
        } else {
            flags |= 0x80;
        }
    }
    if (_ACTCharStatus_Check(self, 5)) {
        flags |= 0x8000;
    }
    if (_ACTCharStatus_Check(self, 23)) {
        flags |= 0x100;
    }
    if (_ACTCharStatus_Check(self, 24)) {
        flags |= 0x40;
    }
    if (_ACTCharStatus_Check(self, 28)) {
        flags |= 0x800;
    }
    if (_ACTCharStatus_Check(self, 3)) {
        flags |= 0x2000;
    }
    if (_ACTCharStatus_Check(self, 2)) {
        flags |= 0x1000;
    }
    if (_ACTCharStatus_Check(self, 6)) {
        flags |= 0x10000;
    }
    if (_ACTCharStatus_Check(self, 7)) {
        flags |= 0x20000;
    }
    if (_ACTCharStatus_Check(self, 30)) {
        flags |= 0x2;
    }
    if (_ACTCharStatus_Check(self, 31)) {
        flags |= 0x4;
    }
    if (_ACTCharStatus_Check(self, 32)) {
        flags |= 0x8;
    }
    if (_ACTCharStatus_Check(self, 4)) {
        flags |= 0x4000;
    }
    if (_ACTCharStatus_Check(self, 25)) {
        flags |= 0x1000000;
    }
    if (_ACTCharStatus_Check(self, 16)) {
        flags |= 0x20;
    }
    if (_ACTCharStatus_Check(self, 12)) {
        flags |= 0x40000;
    }
    if (_ACTCharStatus_Check(self, 0)) {
        flags |= 0x80000;
    }
    if (_ACTCharStatus_Check(self, 1)) {
        flags |= 0x100000;
    }
    if (_ACTCharStatus_Check(self, 15)) {
        if (((Act *)s)->statusVal17 < 1000.0f) {
            flags |= 0x400000;
        }
        if (((Act *)s)->statusVal17 < 300.0f) {
            flags |= 0x800000;
        }
    }
    for (i = 0; i < 25; i++) {
        int kind = lookTargetData[i].kind[col];
        if ((flags >> i) & 1) {
            if (GetTarget(self, s, kind, pos, &mode) != 0) {
                found = 1;
                break;
            }
        }
    }
    if (found != 0) {
        ICO_RAW(float, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x390,
                ((float *)GOBJ_SUB(self)->root.lookPos)[0]) = pos[0];
        ICO_RAW(float, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x394,
                ((float *)GOBJ_SUB(self)->root.lookPos)[1]) = pos[1];
        ICO_RAW(float, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x398,
                ((float *)GOBJ_SUB(self)->root.lookPos)[2]) = pos[2];
        ICO_RAW(int, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x380,
                GOBJ_SUB(self)->root.lookMode) = mode;
        if (self == (girlGObj)) {
            debug_NMarker((float *)GOBJ_SUB(self)->root.lookPos, 0xFF, 0xFF, 0xFF, 100.0f);
        }
    } else {
        GOBJ_SUB(self)->root.lookMode = 0;
    }
}

static void ACTLookTargetSystem_Exec_jun26(GObj *self)
{
    char *s = (char *)GOBJ_ACT(self);

    float pos[4];
    int mode = 1;
    int found = 0;
    int col = ((Act *)s)->actKind;
    int flags;
    int i;

    flags = 0;
    if (_ACTCharStatus_Check(self, 27)) {
        flags = 1;
    }
    if (_ACTCharStatus_Check(self, 8)) {
        flags |= 0x20000;
    }
    if (_ACTCharStatus_Check(self, 9)) {
        flags |= 0x100;
    }
    if (_ACTCharStatus_Check(self, 11)) {
        flags |= 0x40;
    }
    if (_ACTCharStatus_Check(self, 3)) {
        flags |= 0x2000;
    }
    if (_ACTCharStatus_Check(self, 21)) {
        flags |= 0x80;
    }
    if (_ACTCharStatus_Check(self, 22)) {
        flags |= 0x20;
    }
    if (_ACTCharStatus_Check(self, 26)) {
        flags |= 0x200;
    }
    if (_ACTCharStatus_Check(self, 1)) {
        flags |= 0x800;
    }
    if (_ACTCharStatus_Check(self, 0)) {
        flags |= 0x400;
    }
    if (_ACTCharStatus_Check(self, 4)) {
        flags |= 0x4000;
    }
    if (_ACTCharStatus_Check(self, 5)) {
        flags |= 0x8000;
    }
    if (_ACTCharStatus_Check(self, 28)) {
        flags |= 0x2;
    }
    if (_ACTCharStatus_Check(self, 29)) {
        flags |= 0x4;
    }
    if (_ACTCharStatus_Check(self, 30)) {
        flags |= 0x8;
    }
    if (_ACTCharStatus_Check(self, 2)) {
        flags |= 0x1000;
    }
    if (_ACTCharStatus_Check(self, 23)) {
        flags |= 0x100000;
    }
    if (_ACTCharStatus_Check(self, 14)) {
        flags |= 0x10;
    }
    if (_ACTCharStatus_Check(self, 10)) {
        flags |= 0x10000;
    }
    if (_ACTCharStatus_Check(self, 13)) {
        if (((Act *)s)->statusVal17 < 1000.0f) {
            flags |= 0x40000;
        }
        if (((Act *)s)->statusVal17 < 300.0f) {
            flags |= 0x80000;
        }
    }
    for (i = 0; i < 21; i++) {
        int kind = lookTargetData[i].kind[col];
        if ((flags >> i) & 1) {
            if (GetTarget(self, s, kind, pos, &mode) != 0) {
                found = 1;
                break;
            }
        }
    }
    if (found != 0) {
        ICO_RAW(float, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x390,
                ((float *)GOBJ_SUB(self)->root.lookPos)[0]) = pos[0];
        ICO_RAW(float, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x394,
                ((float *)GOBJ_SUB(self)->root.lookPos)[1]) = pos[1];
        ICO_RAW(float, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x398,
                ((float *)GOBJ_SUB(self)->root.lookPos)[2]) = pos[2];
        ICO_RAW(int, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x380,
                GOBJ_SUB(self)->root.lookMode) = mode;
        if (self == (girlGObj)) {
            debug_NMarker((float *)GOBJ_SUB(self)->root.lookPos, 0xFF, 0xFF, 0xFF, 100.0f);
        }
    } else {
        GOBJ_SUB(self)->root.lookMode = 0;
    }
}

static void ACTLookTargetSystem_Exec_aug6(GObj *self)
{
    char *s = (char *)GOBJ_ACT(self);

    float pos[4];
    int mode = 1;
    int found = 0;
    int col = ((Act *)s)->actKind;
    int flags;
    int i;

    flags = 0;
    if (_ACTCharStatus_Check(self, 29)) {
        flags = 1;
    }
    if (_ACTCharStatus_Check(self, 26)) {
        if (GOBJ_WORK(self)->boyDist < 300.0f) {
            flags |= 0x10;
        } else {
            flags |= 0x200;
        }
    }
    if (_ACTCharStatus_Check(self, 10)) {
        flags |= 0x200000;
    }
    if (_ACTCharStatus_Check(self, 11)) {
        flags |= 0x400;
    }
    if (_ACTCharStatus_Check(self, 13)) {
        flags |= 0x80;
    }
    if (_ACTCharStatus_Check(self, 5)) {
        flags |= 0x8000;
    }
    if (_ACTCharStatus_Check(self, 23)) {
        flags |= 0x100;
    }
    if (_ACTCharStatus_Check(self, 24)) {
        flags |= 0x40;
    }
    if (_ACTCharStatus_Check(self, 28)) {
        flags |= 0x800;
    }
    if (_ACTCharStatus_Check(self, 3)) {
        flags |= 0x2000;
    }
    if (_ACTCharStatus_Check(self, 2)) {
        flags |= 0x1000;
    }
    if (_ACTCharStatus_Check(self, 6)) {
        flags |= 0x10000;
    }
    if (_ACTCharStatus_Check(self, 7)) {
        flags |= 0x20000;
    }
    if (_ACTCharStatus_Check(self, 30)) {
        flags |= 0x2;
    }
    if (_ACTCharStatus_Check(self, 31)) {
        flags |= 0x4;
    }
    if (_ACTCharStatus_Check(self, 32)) {
        flags |= 0x8;
    }
    if (_ACTCharStatus_Check(self, 4)) {
        flags |= 0x4000;
    }
    if (_ACTCharStatus_Check(self, 25)) {
        flags |= 0x1000000;
    }
    if (_ACTCharStatus_Check(self, 16)) {
        flags |= 0x20;
    }
    if (_ACTCharStatus_Check(self, 12)) {
        flags |= 0x40000;
    }
    if (_ACTCharStatus_Check(self, 0)) {
        flags |= 0x80000;
    }
    if (_ACTCharStatus_Check(self, 1)) {
        flags |= 0x100000;
    }
    if (_ACTCharStatus_Check(self, 15)) {
        if (((Act *)s)->statusVal17 < 1000.0f) {
            flags |= 0x400000;
        }
        if (((Act *)s)->statusVal17 < 300.0f) {
            flags |= 0x800000;
        }
    }
    for (i = 0; i < 25; i++) {
        int kind = lookTargetData[i].kind[col];
        if ((flags >> i) & 1) {
            if (GetTarget(self, s, kind, pos, &mode) != 0) {
                found = 1;
                break;
            }
        }
    }
    if (found != 0) {
        ICO_RAW(float, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x390,
                ((float *)GOBJ_SUB(self)->root.lookPos)[0]) = pos[0];
        ICO_RAW(float, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x394,
                ((float *)GOBJ_SUB(self)->root.lookPos)[1]) = pos[1];
        ICO_RAW(float, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x398,
                ((float *)GOBJ_SUB(self)->root.lookPos)[2]) = pos[2];
        ICO_RAW(int, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x380,
                GOBJ_SUB(self)->root.lookMode) = mode;
        if (self == (girlGObj)) {
            debug_NMarker((float *)GOBJ_SUB(self)->root.lookPos, 0xFF, 0xFF, 0xFF, 100.0f);
        }
    } else {
        GOBJ_SUB(self)->root.lookMode = 0;
    }
}

void ACTLookTargetSystem_Exec(GObj *self)
{
    if (ICO_JUN26) {
        ACTLookTargetSystem_Exec_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        ACTLookTargetSystem_Exec_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        ACTLookTargetSystem_Exec_aug6(self);
        return;
    }
    char *s = (char *)GOBJ_ACT(self);

    float pos[4];
    int mode = 1;
    int found = 0;
    int col = ((Act *)s)->actKind;
    int flags;
    int i;

    flags = 0;
    if (_ACTCharStatus_Check(self, 32)) {
        flags = 1;
    }
    if (_ACTCharStatus_Check(self, 13)) {
        flags |= 0x10;
    }
    if (_ACTCharStatus_Check(self, 14)) {
        flags |= 0x20;
    }
    if (_ACTCharStatus_Check(self, 28)) {
        if (GOBJ_WORK(self)->boyDist < 300.0f) {
            flags |= 0x40;
        } else {
            flags |= 0x800;
        }
    }
    if (_ACTCharStatus_Check(self, 10)) {
        flags |= 0x800000;
    }
    if (_ACTCharStatus_Check(self, 11)) {
        flags |= 0x1000;
    }
    if (_ACTCharStatus_Check(self, 15)) {
        if (self == (girlGObj)) {
            if ((int)(((Act *)s)->flags20.ll >> 14) & 1) {
                flags |= 0x200;
            }
        } else {
            flags |= 0x200;
        }
    }
    if (_ACTCharStatus_Check(self, 5)) {
        flags |= 0x20000;
    }
    if (_ACTCharStatus_Check(self, 25)) {
        flags |= 0x400;
    }
    if (_ACTCharStatus_Check(self, 26)) {
        flags |= 0x100;
    }
    if (_ACTCharStatus_Check(self, 31)) {
        flags |= 0x2000;
    }
    if (_ACTCharStatus_Check(self, 3)) {
        flags |= 0x8000;
    }
    if (_ACTCharStatus_Check(self, 2)) {
        flags |= 0x4000;
    }
    if (_ACTCharStatus_Check(self, 6)) {
        flags |= 0x40000;
    }
    if (_ACTCharStatus_Check(self, 7)) {
        flags |= 0x80000;
    }
    if (_ACTCharStatus_Check(self, 33)) {
        flags |= 0x2;
    }
    if (_ACTCharStatus_Check(self, 34)) {
        flags |= 0x4;
    }
    if (_ACTCharStatus_Check(self, 35)) {
        flags |= 0x8;
    }
    if (_ACTCharStatus_Check(self, 4)) {
        flags |= 0x10000;
    }
    if (_ACTCharStatus_Check(self, 27)) {
        flags |= 0x4000000;
    }
    if (_ACTCharStatus_Check(self, 18)) {
        flags |= 0x80;
    }
    if (_ACTCharStatus_Check(self, 12)) {
        flags |= 0x100000;
    }
    if (_ACTCharStatus_Check(self, 0)) {
        flags |= 0x200000;
    }
    if (_ACTCharStatus_Check(self, 1)) {
        flags |= 0x400000;
    }
    if (_ACTCharStatus_Check(self, 17)) {
        if (((Act *)s)->statusVal17 < 1000.0f) {
            flags |= 0x1000000;
        }
        if (((Act *)s)->statusVal17 < 300.0f) {
            flags |= 0x2000000;
        }
    }
    for (i = 0; i < 27; i++) {
        int kind = lookTargetData[i].kind[col];
        if ((flags >> i) & 1) {
            if (GetTarget(self, s, kind, pos, &mode) != 0) {
                found = 1;
                break;
            }
        }
    }
    if (found != 0) {
        ICO_RAW(float, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x390,
                ((float *)GOBJ_SUB(self)->root.lookPos)[0]) = pos[0];
        ICO_RAW(float, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x394,
                ((float *)GOBJ_SUB(self)->root.lookPos)[1]) = pos[1];
        ICO_RAW(float, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x398,
                ((float *)GOBJ_SUB(self)->root.lookPos)[2]) = pos[2];
        ICO_RAW(int, ((IntFloat *)&((struct GObj *)self)->dobj)->i, 0x380,
                GOBJ_SUB(self)->root.lookMode) = mode;
        if (self == (girlGObj)) {
            debug_NMarker((float *)GOBJ_SUB(self)->root.lookPos, 0xFF, 0xFF, 0xFF, 100.0f);
        }
    } else {
        GOBJ_SUB(self)->root.lookMode = 0;
    }
}

inline float _ACTGame_GetParamF(int idx)
{
    return gameParam[idx];
}

static inline void ACTGame_SendSoundMail_jun26(GObj *self, int mail, GObj *from, int mot,
                                               int waitSkip)
{
    switch (mail + 50) { /* the demo's ids 50 lower */
    case 0x1A0:
        if (waitSkip != 0 && GOBJ_ACT(self)->soundWait > 0) {
            break;
        }
        iosOmSendMail(self, 366, from);
        if (mot == 0) {
            break;
        }
        {
            Act *act = GOBJ_ACT(self);
            act->soundMot = mot;
            *(unsigned long long *)&act->soundFlag =
                (*(unsigned long long *)&act->soundFlag & ~1ULL) | (waitSkip & 1);
        }
        break;

    case 0x1A1:
        iosOmSendMail(self, 367, from);
        {
            Act *act = GOBJ_ACT(self);
            act->soundMot = mot;
        }
        break;
    }
}

inline void ACTGame_SendSoundMail(GObj *self, int mail, GObj *from, int mot, int waitSkip)
{
    if (ICO_JUN26) {
        ACTGame_SendSoundMail_jun26(self, mail, from, mot, waitSkip);
        return;
    }
    switch (mail + PAL_NTSC_AUG6JUN26(0, 30, 34)) { /* NTSC ids 30 lower, AUG6 ids 34 lower */
    case 0x1A0:
        if (waitSkip != 0 && GOBJ_ACT(self)->soundWait > 0) {
            break;
        }
        iosOmSendMail(self, PAL_NTSC_AUG6JUN26(0x1A0, 386, 382), from);
        if (mot == 0) {
            break;
        }
        {
            Act *act = GOBJ_ACT(self);
            act->soundMot = mot;
            *(unsigned long long *)&act->soundFlag =
                (*(unsigned long long *)&act->soundFlag & ~1ULL) | (waitSkip & 1);
        }
        break;

    case 0x1A1:
        iosOmSendMail(self, PAL_NTSC_AUG6JUN26(0x1A1, 387, 383), from);
        {
            Act *act = GOBJ_ACT(self);
            act->soundMot = mot;
        }
        break;
    }
}

/* PC port: port/platform/diag_host.h (rate-limited log lines: an item
   taken, and the North American twin's mode-2 null read skipped) */
void ico_host_item_held(int kind, int motion, int girl);
void ico_host_item_watch_null(int motion, float frame, float anim_frame);

static void ItemHold(Act *sub, GObj *self) /* derived name */
{
    if (sub->heldItem.i != 0) {
        return;
    }
    if ((sub->heldItem.i = sub->nextItem.i) == 0) {
        return;
    }
    HoldItem(sub->heldItem.p, self);
    /* PC port: a line per item taken (rate-limited), so a log shows pickups happen */
    ico_host_item_held(GetItemKind(sub->heldItem.p), GOBJ_SUB(self)->ctrl.motion, self == girlGObj);
}

static void ItemRelease(Act *sub) /* derived name */
{
    if (sub->heldItem.p == 0) {
        return;
    }
    ReleaseItem(sub->heldItem.p);
    sub->nextItem.p = sub->heldItem.p = 0;
}

static void ACTItemThrow(Act *sub, GObj *self)
{
    float v[4];
    int kind;

    if (sub->heldItem.i == 0) {
        return;
    }
    sceVu0ScaleVector(v, test_CURRENTORIENT(self),
                      PAL_JPNKORCN(_ACTGame_GetParamF(9) + _ACTGame_GetParamF(9),
                                   _ACTGame_GetParamF(PAL_JUN26(9, 8)) * 60.0f /
                                       (float)((60 - systemStatus[0] * 10) / systemStatus[1])));
    kind = GetItemKind(sub->heldItem.p);
    if (ICO_JUN26 ? GOBJ_SUB(sub->heldItem.p)->modelId == 65 : (kind == 1 || kind == 6)) {
        debug_StdPrintfDummy("BOMB!!\n");
        v[0] *= 0.5f;
        v[1] -= 25.0f;
        v[2] *= 0.5f;
    }
    ThrowItem(sub->heldItem.p, v);
    sub->nextItem.i = sub->heldItem.i = 0;
}

static void ACTItemWatchMotion_ntscaug6jun26(GObj *self)
{
    Act *sub = GOBJ_ACT(self);
    /* PC port: the disc's own build reads the item mode at motion bits 16-18; the host loader
       holds it at the European bits 19-21 (config/motion_bits.ntsc.txt, modeBits), like the
       European arm below */
    MotionRec *rec = &motionKind[GOBJ_SUB(self)->ctrl.motion];
    int mode = rec->modeBits.w >> 19;
    int frame = rec->modeBits.b;

    mode &= 7;

    switch (mode) {
    case 2:
        if ((float)frame < GOBJ_SUB(self)->ctrl.animFrame) {
            ItemHold(sub, self);
        } else if (sub->heldItem.i != 0) {
            /* PC port: the 2001 builds read a null item here (the EE reads
               its low RAM, the host faults); the later builds' guard, as
               the European arm below */
            float pos[4];
            GetRootPosition(pos, sub->heldItem.p);
            SetDirectRootPositionNoFittingWithNodePoint(self, 0x16, pos, 0.2f);
            debug_StdPrintfDummy("!!\n");
        } else {
            /* PC port: the null read skipped, logged (rate-limited) */
            ico_host_item_watch_null(GOBJ_SUB(self)->ctrl.motion, (float)frame,
                                     GOBJ_SUB(self)->ctrl.animFrame);
        }
        break;

    case 4:
        if (GOBJ_SUB(self)->ctrl.animFrame < (float)frame) {
            ItemHold(sub, self);
        } else {
            ItemRelease(sub);
        }
        break;

    case 3:
        if (GOBJ_SUB(self)->ctrl.animFrame < (float)frame) {
            ItemHold(sub, self);
        } else {
            ACTItemThrow(sub, self);
        }
        break;

    case 1:
        ItemHold(sub, self);
        break;

    case 0:
        ItemRelease(sub);
        break;
    }

    if (self == boyGObj) {
        sub->curItem = sub->heldItem.p;
        SetBoyInfo(sub->weapon, (void *)sub->heldItem.p);
    }
}

static void ACTItemWatchMotion(GObj *self)
{
    if (ICO_NTSCAUG6JUN26) {
        ACTItemWatchMotion_ntscaug6jun26(self);
        return;
    }
    MotionRec *rec = &motionKind[GOBJ_SUB(self)->ctrl.motion];
    Act *sub = GOBJ_ACT(self);
    int mode = rec->modeBits.w >> 19;
    int frame = rec->modeBits.b;

    mode &= 7;

    if (self == (girlGObj) && boyGObj != 0) {
        if (sub->nextItem.p != 0) {
            Act *o = GOBJ_ACT(boyGObj);
            if (sub->nextItem.p == o->nextItem.p || sub->nextItem.p == o->heldItem.p) {
                sub->nextItem.p = 0;
            }
        }
        if (sub->heldItem.p != 0) {
            Act *o = GOBJ_ACT(boyGObj);
            if (sub->heldItem.p == o->nextItem.p || sub->heldItem.p == o->heldItem.p) {
                sub->heldItem.p = 0;
            }
        }
    }

    switch (mode) {
    case 2:
        if ((float)frame < GOBJ_SUB(self)->ctrl.animFrame) {
            ItemHold(sub, self);
        } else if (sub->heldItem.i != 0) {
            float pos[4];
            GetRootPosition(pos, sub->heldItem.p);
            SetDirectRootPositionNoFittingWithNodePoint(self, 0x16, pos, 0.2f);
            debug_StdPrintfDummy("!!\n");
        }
        break;

    case 4:
        if (GOBJ_SUB(self)->ctrl.animFrame < (float)frame) {
            ItemHold(sub, self);
        } else {
            ItemRelease(sub);
        }
        break;

    case 3:
        if (GOBJ_SUB(self)->ctrl.animFrame < (float)frame) {
            ItemHold(sub, self);
        } else {
            ACTItemThrow(sub, self);
        }
        break;

    case 1:
        ItemHold(sub, self);
        break;

    case 0:
        ItemRelease(sub);
        break;
    }

    if (self == boyGObj && itemWatchOff == 0) {
        sub->curItem = sub->heldItem.p;
        SetBoyInfo(sub->weapon, (void *)sub->heldItem.p);
    }
    if (self == (girlGObj)) {
        sub->curItem = sub->heldItem.p;
        if (mode != 0) {
            GObj *item = sub->heldItem.p;
            int drop = 0;
            if (item != 0) {
                drop = item->active == 0;
            }
            /* two word tests: as member tests gcc folds them into one doubleword load */
            if (sub->heldItem.i == 0 && ICO_RAW(int, sub, 0x184, sub->nextItem.i) == 0) {
                drop = 1;
            }
            if (drop) {
                sub->curItem = sub->heldItem.p = sub->nextItem.p = 0;
                ACTSendMailCorrect(self, 0x7E);
            }
        }
    }
}

inline void ACTItemForceDrop(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    GObj *item = s->heldItem.p;
    if (item != 0) {
        ReleaseItem(item);
        s->heldItem.i = 0;
        s->nextItem.i = 0;
    }
}

static void ACTGame_InsertCamera_GirlIsPinch_jun26(void)
{
    float p0[4];
    float p1[4];
    float p2[4];

    if (boyGObj == 0 || ((char *)girlGObj) == 0) {
        return;
    }
    GetRootPosition(p0, boyGObj);
    GetRootPosition(p1, girlGObj);
    if (_DistSqGV((int *)p0, p1) < 22500.0f) {
        return;
    }
    if (IsPointIsInScreen(p2, test_CURRENTROOT(girlGObj)) > 0.0f) {
        return;
    }
    sceVu0ScaleVector(p0, (float *)GetCurrentCameraSet2(), -1.0f);
    sceVu0ScaleVector(p1, test_CURRENTROOT(girlGObj), -1.0f);
    InsertCamera_Set(p0, p1,
                     _ACTGame_GetParamF(9) *
                         (float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f);
}

void ACTGame_InsertCamera_GirlIsPinch(void)
{
    if (ICO_JUN26) {
        ACTGame_InsertCamera_GirlIsPinch_jun26();
        return;
    }
    float p0[4];
    float p1[4];
    float p2[4];

    if (boyGObj == 0 || ((char *)girlGObj) == 0) {
        return;
    }
    GetRootPosition(p0, boyGObj);
    GetRootPosition(p1, girlGObj);
    if (_DistSqGV((int *)p0, p1) < 22500.0f) {
        return;
    }
    if (IsPointIsInScreen(p2, test_CURRENTROOT(girlGObj)) > 0.0f) {
        return;
    }
    if (ICO_NTSCAUG6JUN26) {
        sceVu0ScaleVector(p0, (float *)GetCurrentCameraSet2(), -1.0f);
        sceVu0ScaleVector(p1, test_CURRENTROOT(girlGObj), -1.0f);
        InsertCamera_Set(p0, p1,
                         _ACTGame_GetParamF(10) *
                             (float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f);
    } else {
        if (boyGObj == 0 || ((char *)girlGObj) == 0) {
            return;
        }
        PrivInsCamSet(test_CURRENTROOT(boyGObj), test_CURRENTROOT((girlGObj)), boyGObj,
                      (60 - systemStatus[0] * 10) / systemStatus[1] * 100 / 60,
                      (60 - systemStatus[0] * 10) / systemStatus[1] * 45 / 60, 0.05f, 0.25f, 1);
    }
}

static void updateHMC(HandModeCmd *hmc, GObj *self, int mode, int pri, int flag, GObj *p5, int p6,
                      float *p7)
{
    hmc->flag = flag;
    hmc->pri = pri;
    switch (mode) {
    case 0:
        GOBJ_SUB(self)->root.hand0.mode = hmc->flag;
        GOBJ_SUB(self)->root.hand0.obj = p5;
        GOBJ_SUB(self)->root.hand0.node = p6;
        if (p7 != 0) {
            GOBJ_SUB(self)->root.hand0.pos[0] = p7[0];
            GOBJ_SUB(self)->root.hand0.pos[1] = p7[1];
            GOBJ_SUB(self)->root.hand0.pos[2] = p7[2];
        }
        break;
    case 1:
        GOBJ_SUB(self)->root.hand1.mode = hmc->flag;
        GOBJ_SUB(self)->root.hand1.obj = p5;
        GOBJ_SUB(self)->root.hand1.node = p6;
        if (p7 != 0) {
            GOBJ_SUB(self)->root.hand1.pos[0] = p7[0];
            GOBJ_SUB(self)->root.hand1.pos[1] = p7[1];
            GOBJ_SUB(self)->root.hand1.pos[2] = p7[2];
        }
        break;
    }
}

void RequestChangeHandMode(GObj *self, int mode, int pri, int flag, GObj *p5, int p6, float *p7)
{
    HandModeCmd *hmc = 0;

    switch (mode) {
    case 0:
        hmc = (HandModeCmd *)&GOBJ_ACT(self)->enemy->handConnect;
        break;
    case 1:
        hmc = (HandModeCmd *)&GOBJ_ACT(self)->enemy->handDisconnect;
        break;
    default:
        debug_assert("src/act-game.c", LN(4727, 4723, 4723, 4417, 4333, 3769, 4723));
        __assert("src/act-game.c", LN(4727, 4723, 4723, 4417, 4333, 3769, 4723), "0");
        break;
    }
    if (pri < 3) {
        if (pri > 0) {
            if (flag == 0) {
                if (hmc->pri != pri) {
                    return;
                }
            }
        }
    }
    if (hmc->pri == 0 || hmc->flag == 0) {
        updateHMC(hmc, self, mode, pri, flag, p5, p6, p7);
    } else if (pri >= hmc->pri) {
        updateHMC(hmc, self, mode, pri, flag, p5, p6, p7);
    }
}

inline void _ACTSetEnemyDisappearSpeed(GObj *self, float speed)
{
    GOBJ_WORK(self)->disappearSpeed = speed;
}

/* a point 300 ahead of the actor's head, toward the girl on another stage
   (June 26's GetGirlPositionAtThisStage(pos, obj); this build also has the
   later one-argument GetGirlPositionAtThisStage) */
static inline void GetGirlPositionAheadOfHead(float *pos, GObj *obj) /* derived name */
{
    float dir[4];
    float head[4];

    GetOtherStageGirlOrient(dir, GetCurrentCameraSet2());
    GetSkeltonPosition(head, obj, 35);
    sceVu0ScaleVector(dir, dir, 300.0f);
    sceVu0AddVector(pos, head, dir);
}

static inline int ACTChkAttackIgnore_BOY_ntsc(GObj *self, GObj *actor)
{
    (void)actor; /* PC port: the European parameter, unused here */
    Act *s = GOBJ_ACT(self);
    if (s->actMode == 53 ||
        (((ActWork *)s->work)->leverTimer != 0 && scpBoyControlReadDisable != 0)) {
        return 1;
    }
    return 0;
}

inline int ACTChkAttackIgnore_BOY(GObj *self, GObj *actor)
{
    if (ICO_NTSC) {
        return ACTChkAttackIgnore_BOY_ntsc(self, actor);
    }
    Act *s = GOBJ_ACT(self);
    if (s->actMode == 0x35 ||
        (((ActWork *)s->work)->leverTimer != 0 && scpBoyControlReadDisable != 0) ||
        ((int)(s->flags18.ll >> 35) & 1) == 0) {
        return 1;
    }
    return 0;
}

static inline int ACTChkAttackIgnore_GIRL_ntsc(GObj *self, GObj *actor)
{
    (void)actor; /* PC port: the European parameter, unused here */
    Act *s = GOBJ_ACT(self);
    if (s->actMode == 107) {
        return 1;
    }
    return 0;
}

inline int ACTChkAttackIgnore_GIRL(GObj *self, GObj *actor)
{
    if (ICO_NTSC) {
        return ACTChkAttackIgnore_GIRL_ntsc(self, actor);
    }
    Act *s = GOBJ_ACT(self);
    switch (s->actMode) {
    case 0x6F:
        return 1;

    case 5:
        if (actor != 0 && actor->kind == 17) {
            return 1;
        }
        break;
    }
    return 0;
}

static inline int ACTChkAttackIgnore_ENEMY_ntsc(GObj *self, GObj *actor)
{
    (void)actor; /* PC port: the European parameter, unused here */
    Act *s = GOBJ_ACT(self);

    if (s->actMode == 100) {
        return 1;
    }
    return 0;
}

inline int ACTChkAttackIgnore_ENEMY(GObj *self, GObj *actor)
{
    if (ICO_NTSC) {
        return ACTChkAttackIgnore_ENEMY_ntsc(self, actor);
    }
    Act *s = GOBJ_ACT(self);

    switch (s->actMode) {
    case 0x67:
        if ((60 - systemStatus[0] * 10) / systemStatus[1] / 2 < s->modeFrame) {
            return 1;
        }
        break;

    case 6:
        if (((int)(s->flags18.ll >> 57) & 1) && stage_no != PAL_JPNKORCN(0x56, 84) &&
            stage_no != 3 && stage_no != PAL_JPNKORCN(0x2E, 46)) {
            return 1;
        }
        break;
    }
    return 0;
}
