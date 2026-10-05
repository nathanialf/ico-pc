/*
 * ico2/fumi/include/commonact.h
 *
 * The declarations of what commonact.c defines, for the files that use
 * them.  The file name is derived.
 */

#ifndef COMMONACT_H
#define COMMONACT_H

#include "typedef.h"

struct GObj;
struct MotOriReq;

void ACTAcceptMail(struct GObj *self, int mail);
void ACTAdjustPlane(struct GObj *self, void *wall); /* wall: the wall record the root is laid against */
int ACTGetOrientFromIntrK(struct GObj *self, int kind, struct MotOriReq *out, int i);

struct IntrMail; /* act.h */

void ACTRunIntrCorrect(struct GObj *self, struct IntrMail *intr, struct IntrMail *corr);
void ACTSendMailCorrect(struct GObj *self, int mail);
void ACTSetPositionWithFitting(void *self, float *pos);
void ACT_LAYOUT_GAMEOVER(void);
void ContinueCorrectPosition(void *obj);
void ControlMotionOrient(int id, int mot);
void GetCorrectOrientOfChain(void *buf, void *obj);
int IsCorrectPosition(struct GObj *self);
int SetMotionDirectionSmooze(struct GObj *self, float *dir, float s);
void StartCorrectPosition(struct GObj *self, float *pos, float *dir, int mode, float t);
int _ACTCorrectMsg(struct GObj *self, int msg, void *arg);
void _ACTDebugPrint(struct GObj *self);
int _ACTMotDirSmzDirect(struct GObj *self, float *dir);
void actAfterDown(struct GObj *volatile self);
void actAfterFly(struct GObj *volatile self);
void actAfterForceRope(struct GObj *volatile self);
void actAfterForceRopeSwing(struct GObj *volatile self);
void afterCommonBar(struct GObj *volatile self);
void afterCommonOneWall(int x);
#ifdef ICO_HOST
void afterCommonRevive(ICO_WORD_PTR(GObj *) volatile self);
#else
void afterCommonRevive(volatile unsigned int self);
#endif
void afterCommonRope(struct GObj *volatile self);
void afterCommonStone(struct GObj *volatile self);
void afterCommonTruckLever(struct GObj *volatile self);
void subCommonIdle(struct GObj *volatile self);
float *test_CURRENTORIENT(struct GObj *self);
float *test_CURRENTROOT(struct GObj *self);
int FloorIsTruck(struct GObj *self);
void afterCommonBox(struct GObj *volatile self);
void actAfterFall(struct GObj *volatile self);

/* idle-mot-def: one idling motion per actor kind, 0x0C bytes. Reader:
 * ico2/fumi/src/commonact.c (int [][3]). Owner: ico2/fumi/include/commonact.h. */
typedef struct {   /* field names derived */
    int motion[3]; /* 0x00, indexed by Act+0x48 */
} IdlingDef;       /* derived name */
extern const IdlingDef idlingDef[];

/* act-data-tbl: one idle-motion range, 0x14 bytes, indexed by Act+0x48.
 * Reader: ico2/fumi/src/commonact.c (SetIdleMotionRange, subCommonIdle).
 * Owner: ico2/fumi/include/commonact.h. */
typedef struct {     /* field names derived */
    int idleMotion;  /* 0x00 */
    int orientRow;   /* 0x04, the motionOrient row whose nextId takes the motion */
    int orientRow2;  /* 0x08, the row that takes the second motion */
    int orientFirst; /* 0x0C, the rows whose id takes the motion */
    int orientEnd;   /* 0x10 */
} IdleRangeRec;      /* derived name */
extern IdleRangeRec actDataTbl[];

/* node-fix-ofs: one cling pose, 0x24 bytes. Reader:
 * ico2/fumi/src/commonact.c (ClingRec, SetMotionNodeFixModeParameter's
 * arguments). Owner: ico2/fumi/include/commonact.h. */
typedef struct {  /* field names derived */
    int rot[3];   /* 0x00, degrees about x, y, z */
    float pos[3]; /* 0x0C */
    int mode;     /* 0x18 */
    int motion;   /* 0x1C, the motion the cling plays */
    int node;     /* 0x20 */
} ClingRec;       /* derived name */
extern ClingRec clingData[];

/* pair-motion: one paired motion, 8 bytes. Reader: ico2/fumi/src/
 * commonact.c (BecPair). Owner: ico2/fumi/include/commonact.h. */
typedef struct { /* field names derived */
    int mot;     /* 0x00 */
    int req;     /* 0x04 */
} BecPair;       /* derived name */
extern const BecPair pairMotion[];

void _ACTCommonMailTest(struct GObj *self, int stopCnt, int walkCnt, int runCnt);

#endif /* COMMONACT_H */
