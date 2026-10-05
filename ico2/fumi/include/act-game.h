/*
 * ico2/fumi/include/act-game.h
 *
 * The declarations of what act-game.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef ACT_GAME_H
#define ACT_GAME_H

#include "typedef.h" /* WayRequest, which ActWork carries at 0x580 */
#include "clipCollisionManager.h"

struct GObj;

/* The pair of hand-link wall probes the debug overlay draws, mirrored into
   the actor work area at +0x540 (act-game.c). */
typedef struct HandClInfo { /* field names derived */
    unsigned char on;   /* 0x00 */
    unsigned char hit;  /* 0x01 */
    unsigned char attr; /* 0x02 */
    char pad3[13];
    long long orient[2]; /* 0x10 -- GetOrientOfWall's output */
    unsigned char hit2;  /* 0x20 */
    unsigned char attr2; /* 0x21 */
    char pad22[14];
    long long orient2[2]; /* 0x30 */
} HandClInfo;             /* derived name */

/* One of the three records ACTGetEnvironment keeps at ActWork + 0x810: a
 * position and direction with a strength and a frame count. */
typedef struct ActEffRec {
    float pos[4]; /* 0x00 */
    float dir[4]; /* 0x10 */
    float power;  /* 0x20 */
    int frames;   /* 0x24 */
    int kind;     /* 0x28 */
    char pad2C[4];
} ActEffRec;

/* The actor's character work, the record at Act+0x688 (held there as a
 * word, like the object's own actor slot): the boy's, the girl's and the
 * enemies' per-character state. */
typedef struct ActWork { /* field names derived */
    int paraTbl[86];        /* 0x000, each parallel slot's motion (ActPara_MakeTbl) */
    char pad158[456];
    float defIkRate0;       /* 0x320 */
    float defIkRate1;       /* 0x324 */
    float defIkRate2;       /* 0x328 */
    float boyDist;          /* 0x32C */
    float escortOffset;     /* 0x330 */
    float disappearSpeed;   /* 0x334 */
    float fallDamageHeight; /* 0x338 */
    float ropeClimbHeight;  /* 0x33C, the chain top over the actor, sent with mail 316 */
    float stickMag;        /* 0x340 */
    float lockedMaxRotate; /* 0x344 */
    float parallelInterp;  /* 0x348 */
    char pad34C[4];
    float padWish[4]; /* 0x350, the pad wish direction a jump turns to (funcCommonJumpDircorrect) */
    float fallDir[4]; /* 0x360, the direction a fall turns to (funcCommonFallDircorrect) */
    struct GObj *hideObj; /* 0x370 */
    void *floorObj;   /* 0x374 */
    void *bga;        /* 0x378 */
    int downTimer;    /* 0x37C */
    int brainTimer;   /* 0x380 */
    int turnTimer;    /* 0x384 */
    int turnTimer2;   /* 0x388 */
    int noInterpTimer;   /* 0x38C */
    int wishHoldTimer;   /* 0x390 */
    int leverTimer;      /* 0x394, frames the boy keeps hold of a lever (actCommonLever) */
    int nakaBossCount;   /* 0x398 */
    int carryGirlFrames; /* 0x39C */
    int sofaTimer;       /* 0x3A0, frames since the actor sat on the sofa (actCommonSofa) */
    int sofaRestTimer;   /* 0x3A4, the longer sofa rest the girl's attract waits out */
    int orientFrames;    /* 0x3A8 */
    int timer3AC;        /* 0x3AC */
    int turnMailWait;    /* 0x3B0, the girl holds back girlAttractTurnMailR while it runs */
    int jumpTimer;       /* 0x3B4 */
    int footIkFrames;    /* 0x3B8 */
    int bit37Frames;     /* 0x3BC */
    int bit38Frames;     /* 0x3C0 */
    int mailB1Timer;     /* 0x3C4 */
    int ditchTimer;      /* 0x3C8 */
    MotOriTarget bellowWall400;  /* 0x3CC, the wall of attribute 0x400 actCommonLadderBellow
                                    found below, which mail 144 turns to */
    MotOriTarget bellowWall3000; /* 0x3D8, the same for attribute 0x3000 and mail 145 */
    struct GObj *dangerObj; /* 0x3E4, the object SetGirlDangerGObj marks for the girl */
    char pad3E8[8];
    float hideDirX; /* 0x3F0 */
    float hideDirY; /* 0x3F4 */
    float hideDirZ; /* 0x3F8 */
    char pad3FC[4];
    void *ropeCage; /* 0x400 */
    char pad404[12];
    float handPosX; /* 0x410, the hand node's position (GetSkeltonPosition's target), x */
    float handPosY; /* 0x414 */
    float handPosZ; /* 0x418 */
    float handPosW; /* 0x41C */
    float lastPosX; /* 0x420, the root position as last seen (ACTGame speed check) */
    float lastPosY; /* 0x424 */
    float lastPosZ; /* 0x428 */
    char pad42C[4];
    float velX; /* 0x430 */
    float velY; /* 0x434 */
    float velZ; /* 0x438 */
    char pad43C[4];
    float speed;    /* 0x440 */
    int slowFrames; /* 0x444 */
    int stopFrames; /* 0x448 */
    char pad44C[4];
    int noMoveFrames;        /* 0x450 */
    unsigned int enemyFlags; /* 0x454 */
    char pad458[8];
    GObj *genTarget;         /* 0x460, the generator the enemy heads for */
    int motherLabel;         /* 0x464 */
    struct GObj *motherGObj; /* 0x468, the generator object motherLabel names */
    char pad46C[4];
    float hangOrient[3]; /* 0x470, the cliff orientation actBoyHangBefore keeps */
    char pad47C[4];
    MotOriReq cliffReq; /* 0x480, the request mails 140 and 305 turn to: b the
                           wall under the cliff edge (actBoyCliffHesitate),
                           a the actor's motion request's b (actBoyHangBefore) */
    float pinchPosX; /* 0x4A0 */
    float pinchPosY; /* 0x4A4 */
    float pinchPosZ; /* 0x4A8 */
    char pad4AC[4];
    int pinchFrames; /* 0x4B0 */
    char pad4B4[12];
    float handrailOrient[4]; /* 0x4C0, the wall orientation actCommonHandrail keeps */
    int basePosSet;          /* 0x4D0 */
    char pad4D4[12];
    float basePos[4]; /* 0x4E0, the position the enemy guards when basePosSet */
    unsigned char boxSideSet; /* 0x4F0, set when the cliff edge is at a box side (ACTGetEnvironment) */
    char pad4F1[15];
    float boxSidePos[4]; /* 0x500, where the actor stands at that box side */
    float boyOrient[4]; /* 0x510, the girl's direction to the boy (ACTGetEnvironment) */
    float hintPosX;     /* 0x520 */
    float hintPosY;     /* 0x524 */
    float hintPosZ;     /* 0x528 */
    char pad52C[4];
    float boxDir[4]; /* 0x530, the direction a box is pushed and pulled in (actCommonBox) */
    HandClInfo handCl; /* 0x540, the hand-link wall probes the debug overlay draws */
    WayRequest wayReq; /* 0x580, the way search RequestWayBegin hands to the way system manager */
    char pad638[232];
    ClipColReq view; /* 0x720, the clip ACTGameView_Loop runs to the object it looks at */
    int viewState; /* 0x800 */
    char pad804[12];
    ActEffRec effRec[3]; /* 0x810, 0x840, 0x870 */
    float emgPosX; /* 0x8A0 */
    float emgPosY; /* 0x8A4 */
    float emgPosZ; /* 0x8A8 */
    char pad8AC[4];
    MotOriReq intrReq; /* 0x8B0, the orient request of the interrupt motion
                          act_check_intr_list started; the hang and climb acts
                          lay the root against its walls, b's element the wall
                          under the cliff edge */
    char pad8D0[16];
    int wayHold; /* 0x8E0, cleared when a detailed way walk starts (ACTWayMove_Begin) */
    char pad8E4[12];
    float wayDirX; /* 0x8F0, the way walk's direction (ACTWayExec_Position) */
    float wayDirY; /* 0x8F4 */
    float wayDirZ; /* 0x8F8 */
    char pad8FC[4];
    int modeHist[10];  /* 0x900, the last ten action modes, newest first (BeforeFunc) */
    int frameHist[10]; /* 0x928, the frame each of them lasted to */
    int prevHist[10];  /* 0x950, the mode before each */
} ActWork; /* derived name */

#define GOBJ_WORK(o) ((ActWork *)GOBJ_ACT(o)->work) /* derived name */

/* act-game.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order). */
int ACTNotNeedCameraOffset(struct GObj *self);
void ACTGameCollisionOn(volatile int *self);
void ACTGameCollisionOff(volatile int *self);
int ACTGame_CheckItemMotion(struct GObj *self);
int ACTGame_CheckHandMotion(struct GObj *boy, struct GObj *girl);
void ACTGame_StageChangeGObjID(int no, int kind, int idx);
void ACTGame_StageChangeGObjDirect(struct GObj *self, int stage, void *dir, int deg);
int ACTGame_FLAG_LIFEPINCH(struct GObj *self);
unsigned char ACTGame_FLAG_TETSUNAGI(void);
int ACTGame_FLAG_TETSUNAGI_VISUAL(void);
inline void GetSkeltonPosition(float *dst, struct GObj *obj, int node);

void SetDirectRootPositionWithNodePointLimit(void *self, int node, void *pos, float t,
                                             float limit);

void ACTGameView_Init(void);
void ACTCharctrl_Lock(struct GObj *self);
void ACTCharctrl_Unlock(struct GObj *self);
void ACTGame_ConnectHand(void);
inline void ACTGame_DisconnectHand(void);
void PAIR_GetPosition_BOY(float *pos, float *dir);
int PAIR_IsStatus_BOY_PULL(void);
int PAIR_IsStatus_GIRL_PULL(void);
int PAIR_IsStatus_BOY_WAIT(void);
void PAIR_GetPosition_BOY_DITCH(float *pos, float *dir);
int PAIR_IsStatus_BOY_DITCH(void);
struct GObj *ACTGame_isHangChain(struct GObj *self);
ICO_WORD_PTR(struct GObj *) ACTGame_isWeaponEnableCatchfire(struct GObj *self);
int ACTCheckCollis_WF(float f, void *p0, void *p1, void *actor, void *posout);

int ACTCheckCollis_W(float f, void *hand0, void *hand1, void *actor, void *posout, void *magtarget,
                     int *flagout);

int ACTCheckCollis_CI(float *start, float *end, int *attr, WallCfg *wallHit);
int ACTCheckCollis_WELL(void *p0, void *p1, void *actor, void *posout, float f);
unsigned char ACTCheckCollis_WAY(float f, void *p0, void *p1, void *actor, void *posout);
int ACTCheckViewCl(struct GObj *self, void *target, void *targetPos, int range, float f);
void ACTGameView_FirstSet(char *self);
void ACTGameView_Add(struct GObj *self, struct GObj *target);
int ACTGameView_Check(struct GObj *self, struct GObj *obj);
int ACTGameViewSimple_Check(struct GObj *self, struct GObj *obj);
int ACTGame_GetMotOrientFromWeapon(struct GObj *weapon);
unsigned char ACTGame_NoWeapon(struct GObj *self);
#ifdef ICO_HOST
inline int ACTGame_isWeaponCombustible(GObj *self);
#else
inline int ACTGame_isWeaponCombustible(void);
#endif
struct GObj *ACTGame_GetNearestGObj(float *pos, int kind);
void ACTLookTarget_Init(struct GObj *self);
int _ACTLookTarget_Set(struct GObj *self, struct GObj *target, float *pos, int pri, int mode);
void ACTParaStatus_Init(struct GObj *self);
inline void _ACTParaStatus_Set(struct GObj *self, int bit);
unsigned long long _ACTParaStatus_Check(struct GObj *self, int bit);
void _ACTCharStatus_Init(int **self);
void _ACTCharStatus_Set(struct GObj *self, int bit, float f, ICO_WORD val);
unsigned char _ACTCharStatus_Check(struct GObj *self, int bit);
void _ACTCharStatus_Exec(void);
void _ACTSetEnemyDisappearSpeed(struct GObj *self, float f);
void ACTGame_SetMotionPlaySpeedRatio_Reserve(struct GObj *self, float f, unsigned int pri);
float _ACTGame_GetParamF(int idx);
int ACTGame_GetCurrentCallStatus(struct GObj *self);
unsigned char ACTGame_CheckPriInputFrame(struct GObj *self);
void ACTGame_SendSoundMail(struct GObj *self, int mail, struct GObj *from, int mot, int waitSkip);
void ACTGame_LwsEffectInit(struct GObj *self);
void ACTGame_LwsEffect_Guard(struct GObj *self);
inline void ActGame_GetOrientQ(void *q, void *v, int deg);
void _GetRootObjectOrient(void *orient, struct GObj *obj);
void ACTItemForceDrop(struct GObj *self);
inline void GetOtherStageGirlOrient(float *orient, float *root);
int ACTChkAttackIgnore_BOY(struct GObj *self, struct GObj *actor);
int ACTChkAttackIgnore_GIRL(struct GObj *self, struct GObj *actor);
int ACTChkAttackIgnore_ENEMY(struct GObj *self, struct GObj *actor);
unsigned char ACTCheckCollis_VIEW(float f, void *p0, void *p1, void *actor);
int ACTCheckViewClDetail(struct GObj *self, void *target, void *targetPos, int range, float f);
void ACTGame_SetMotionPlaySpeedRatio_Clear(struct GObj *self);
void ACTGame_SetMotionPlaySpeedRatio_Exec(struct GObj *self);
void GetGirlPositionAtThisStage(float *pos);
int ACTCheckView(struct GObj *self, void *target, void *targetPos, int range, float f);
void ACTGame_BeforeFunc(struct GObj *self);
void ACTGame_InsertCamera_GirlIsPinch(void);
void ACTParaStatus_Clear(struct GObj *self);
void ACTGame_SaveActorInformation(struct GObj *self);
void ACTGame_DeleteActorInformation(struct GObj *self);
/* The second parameter is an unsigned char. */
void ACTGame_SetActors_Debug(int stage, unsigned char flag);
void ACTGame_StageChangeGObj(struct GObj *self, int idx);
void GetSkeltonOrient(float *out, void *obj, int node);
void RequestChangeHandMode(GObj *self, int mode, int pri, int flag, GObj *p5, int p6, float *p7);

int _ACTGame_SearchGObj(struct GObj *self, struct GObj *tgt, float range, float height, int angle,
                        float *out);

/* act-game.o's last two .sdata globals: the floor and wall records
 * ACTCheckCollis_WELL and ACTCheckCollis_WAY publish. */
extern void *floorGObj_ACTCheckCollis_WELL;
extern void *wallGObj_ACTCheckCollis_WAY;

/* a 64-bit status word, read whole or as two words */
typedef union { /* field names derived */
    unsigned long long q;
    unsigned int w[2];
} ActStatusWord; /* derived name */

void ACTGameView_Loop(struct GObj *self);

/* look-target-data: the look target kinds of one entry, 0x0C bytes, 27
 * rows, one column per character kind (Act+0x48). Reader:
 * ico2/fumi/src/act-game.c. Owner: ico2/fumi/include/act-game.h. */
typedef struct { /* field names derived */
    int kind[3]; /* 0x00, one per column */
} LookTarget;    /* derived name */

extern const LookTarget lookTargetData[];
void ACTGame_CommonLoop(struct GObj *self);
void ACTParaStatus_Exec(struct GObj *self);
void ACTLookTargetSystem_Exec(struct GObj *self);
extern float gameParam[]; /* game-param: the tuning values _ACTGame_GetParamF returns */

#endif /* ACT_GAME_H */
