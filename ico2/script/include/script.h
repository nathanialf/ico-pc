/*
 * ico2/script/include/script.h
 *
 * The declarations of what script.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef SCRIPT_H
#define SCRIPT_H

#include "typedef.h"

struct GObj;

struct SqEntry;

/* script.o's .sdata globals: the boy-control read lock the
   scripts raise, the sound-environment master volume rate, the statue's
   common ADPCM handle, two dummy GObj words and the statue's pad vibration
   handle and volume. */
extern int scpBoyControlReadDisable;
extern float scpSeEnvMasterVolRate;
extern struct SqEntry *sekizo_common;
extern char *scpDummyGObj;
extern char *scpDummyGObj2;
extern int sekizo_yure;
extern unsigned char sekizo_yure_vol;
/* script.c's `inline` functions, in the order of their definitions'
 * out-of-line copies at the end of the object (first-declaration order),
 * from scpDispOffAllWithKind on. */
void scpDispOffAllWithKind(int kind);
void scpDispOnAllWithKind(int x);
void scpActivateAllWithKind(int kind);
void scpDisActivateAllWithKind(int kind);
void scpLinkBGAtoLayoutedTarget(int id, int key);
void scpLinkBGAtoLayoutedTargetSkelton(int id, int focus, int key);
void scpLinkBGAtoLayoutedTargetSkeltonWithLocalRotationFlag(int id, int focus, int key, int localRotation);
inline void scpLinkBGAtoKindTargetSkeltonWithLocalRotationFlag(int kind, int focus, int key, int localRotation);
WallCfg *scpGetWallCollision(float x0, float y0, float z0, float x1, float y1, float z1);
void scpDoorTypeUp(struct GObj *volatile self);
inline void scpDoorTypeUpSwitch(struct GObj *volatile self);
void scpAdpcmPlayRequestFunc(int kind, struct SqEntry **id, int ch, int loopNum, int play);
int scpAdpcmPlayRequestNum(void);
int scpAdpcmFadeCloseFunc(struct SqEntry **h, short fade);
int scpAdpcmCloseChkFunc(struct SqEntry **h);
void scpDeamon(struct GObj *volatile self);
void scpGirlHintVoiceCancel(void);
void scpWoodBox(struct GObj *volatile self);
int scpIsTorchLightOn(int id);
struct GObj *scpIsBombExplode(int x);
float scpGetRotObjectRotCount(int id);
int scpIsRotObjectZPlusDirInclude(int id, int from, int to);
void scpTransLinear(struct GObj *obj, int axis, float target, float step);
void scpRotateLinear(void *obj, int deg, short step, int axis);
int scpTriggerPosBall(float *pos, float *target, float r);
int scpTriggerBall(struct GObj *obj, struct GObj *target, float r);
int scpTriggerFloorAttr(struct GObj *self, int attr);
int scpTriggerWallAttr(struct GObj *self, int attr);
int scpTriggerFloorAttrTargetMan(struct GObj *self, int attr);
int scpTriggerPosBox(float *p, float *pos, float *size);
int scpEffectStart(void *pos, int kind);
void scpSleepEnemyAll(void);
void scpWakeupEnemyAll(void);
inline void scpKillEnemyAll(void);
inline void scpMaskGeneratorAll(void);
void scpKillEnemyOne(int id);
int _SCPMoveCharactorByWay(struct GObj *self, ICO_WORD tgt, float *dir, float speed, int flags);

int _SCPMoveByWay_ToChar(struct GObj *self, struct GObj *target, int deg, int flags, float scale,
                         float speed);

void _SCPCharacterStop(struct GObj *self);
inline struct GObj *scpSearchGobj(int id);
void scpPlayMotNode(void *self, int mot, void *obj, int node);
void scpPlayMotReq(struct GObj *self, int mot);
void scpPlayPosSet(void *self, float x, float y, float z);
void scpPlayWaitMotEnd(struct GObj *self);
void InitStageChange(void);
int RequestStageChange(int no, struct GObj *g, struct GObj *girl, float speed, float wait);

inline int RequestStageChangeWithColor(int no, struct GObj *g, struct GObj *girl, float speed,
                                       float wait, unsigned char r, unsigned char gr,
                                       unsigned char b);

int RequestStageChangeSimple(int no, float speed, float wait, unsigned char r, unsigned char gr,
                             unsigned char b);

void RequestStageChangeDirect(struct GObj *self, int stage, void *dir, int deg);
inline void scpFadeOut(float speed, int r, int g, int b);
inline void scpFadeIn(float f);
int scpFadeChk(void);
int scpGameStat_BoyWeaponkind(void);
int scpIsWallLever2On(void);
int scpIsHangChain(struct GObj *self);
int scpIsHangChainOptional(struct GObj *self, int b);
void scpBornSpider(int n, float a, float b, float c, float d);
inline int scpActStatusDeathFall(struct GObj *self);
void scpSetStreamMotionRootOffset(struct GObj *self, float x, float y, float z);
void scpWakeupItemWithBoundary(float x, float y, float z, float r);
int scpCheckReadyAllObjects(void);
inline void ScpCallCameraSetTarget(float x, float y, float z);
void ScpCallCameraGetTarget(float *dst);
void ScpCallCameraOff(void);
void ScpCallCameraOn(void);
void ScpCallCameraTargetOff(void);
void scpTransGObj(void *self, float dx, float dy, float dz);
void scpExplodeSecretItem(void);
int scpCheckExistAliveEnemy(void);
int scpCheckExistAliveSpider(void);
void scpLockMaxRotate(struct GObj *self, float rot);
void scpUnLockMaxRotate(struct GObj *self);
short scpGetRotObjectCurrentRot(int no);
void scpCheckDisconnectWallStart(struct GObj *self);
void scpCheckDisconnectWallEnd(struct GObj *self);
int scpTriggerIgnore(struct GObj *self);
inline void scpDoorTypeUpMain(struct GObj *volatile self);
void actSubSekizoSe(struct GObj *volatile self);
/* The entry points script.o compiles in place, in its own parse order. */
void scpTorchLightOn(int id);
void scpTorchLightOff(int id);
void scpSetCageVelocityFriction(int id, float friction);
void scpPlayMotDir(struct GObj *self, float *dir);
void scpPlayMot(struct GObj *self, int mot);
void scpPlayStart(struct GObj *self);
void scpPlayEnd(struct GObj *self);
void scpDoorTypeUpDown(struct GObj *volatile self);
void scpDoorTypeUpUp(struct GObj *volatile self);
void scpSubAdpcmPlay(struct GObj *volatile self);
void scpAdpcmCloseFunc(struct SqEntry **h);
void scpGirlHintVoiceReady(int kind);
void scpGirlHintVoicePlay(void);
void scpGirlHintVoiceTickProc(void *cam);

void scpSekizou(struct GObj *self, int flag, int anim, int anim2, int kind, float bx, float by,
                float bz, float gx, float gy, float gz);

void _SCPMoveCharactorByWay_Cancel(struct GObj *self);
void scpSekizouCheckPoint(void);
void scpWakeupEnemyOne(int id);
void scpSleepEnemyOne(int id);
void scpSleepSpiderGroupOne(int id);
void scpWakeupSpiderGroupOne(int id);
void scpKillSpiderGroup(int id);
void preload(int idx);
void scpSetBoyWeaponGObj(void *w);

/* one entry of girlWarpList, the girl's warp table, 0x58 bytes; warpGirl.c
 * reads it */
typedef struct {         /* field names derived */
    float arrive2[4];    /* 0x00, the arrival in to2: y angle, then z, y, x */
    float arrive1[4];    /* 0x10, the arrival in to1 */
    float arrive0[4];    /* 0x20, the arrival in to0 */
    int gflag;           /* 0x30 */
    float box0[3];       /* 0x34 */
    float box1[3];       /* 0x40 */
    unsigned short to0;  /* 0x4C */
    unsigned short to1;  /* 0x4E */
    unsigned short to2;  /* 0x50 */
    unsigned short from; /* 0x52 */
    unsigned char kind;  /* 0x54 */
    char pad55[3];
} WarpRec; /* derived name */

extern const WarpRec girlWarpList[];

#endif /* SCRIPT_H */
