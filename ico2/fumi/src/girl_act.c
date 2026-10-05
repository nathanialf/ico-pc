#include "ee_view.h"
#include "girl_act.h"
#include "debug.h"
#include "pad.h"
#include "obj_manager.h"
#include "act-way.h"
#include "way_sys.h"
#include "brain.h"
#include "gflag.h"
#include "Primitive.h"
#include "box.h"
#include "matrixDrive.h"
#include "motionOrientManager.h"
#include "quaternion.h"
#include <stdlib.h>
#include "geometryManager.h"
#include "typedef.h"
#include "script.h"
#include <libvu0.h>
#include "wireLetter.h"
#include "GifPacket.h"
#include "isys.h"
#include "debug_exception.h"
#include <string.h>
#include "generator.h"
#include "boyact.h"
#include "lws_kyomi.h"
#include "act.h"
#include "gobj.h"
#include "enemy_act.h"
#include "torch.h"
#include <stdio.h>
#include "act-game.h"
#include "commonact.h"
#include "gv.h"
#include "main.h"
#include "fieldCollision.h"
#include <assert.h>
#include "camera-editor.h"
#include "motionManager2.h"
#include "act-env.h"
#include "item.h"
#include "wireLetter.h"

static void _girlBrainHide_MakeHidePoint(float *p, float dist);
static int isEnterHideadv(void);

typedef struct GirlStand { /* field names derived */
    sceVu0FVECTOR prev;    /* 0x00 last frame's root position */
    sceVu0FVECTOR cur;     /* 0x10 this frame's root position */
    float moveDist;        /* 0x20 how far the root moved this frame */
    char pad24[12];
    sceVu0FVECTOR orient;    /* 0x30 the girl's facing */
    sceVu0FVECTOR toBoy;     /* 0x40 the direction from the girl to the boy */
    float handDist;          /* 0x50 HandMgr_GetDistHand: girl's hand node to the boy's */
    float handHeight;        /* 0x54 the hands' height gap */
    unsigned char still;     /* 0x58 moveDist under 2 ("gv") */
    unsigned char turned;    /* 0x59 facing 61 degrees or more off toBoy ("mo") */
    unsigned char far100;    /* 0x5A handDist over 100 */
    unsigned char far125;    /* 0x5B handDist over 125 ("hd") */
    unsigned char far135;    /* 0x5C handDist over 135 */
    unsigned char near90;    /* 0x5D handDist under 90 ("hd2") */
    unsigned char heightGap; /* 0x5E handHeight over 15 */
} GirlStand;                 /* derived name */

union GAIF { /* field names derived */
    int i;
    float f;
}; /* derived name */

static void GetEyeDirection(char *dir, GObj *obj)
{
    int node = GetSkeltonFocusNode(obj, 0x23);
    if (obj->kind == 4) {
        *(int *)(dir + 0x0) = 0;
        ((union GAIF *)(dir + 0x4))->f = -1.0f;
        *(int *)(dir + 0x8) = 0;
    } else {
        *(int *)(dir + 0x0) = 0;
        ((union GAIF *)(dir + 0x4))->f = 1.0f;
        *(int *)(dir + 0x8) = 0;
    }
    *(int *)(dir + 0xC) = 0;
    sceVu0ApplyMatrix(dir, (char *)(GOBJ_SUB(obj)->nodeMtx + (node << 6)), dir);
}

void funcGirlHandDisconnect(void)
{
    ACTGame_DisconnectHand();
    debug_StdPrintfDummy("--disconnect--\n");
}

/* The three climb headers (omori/include/g50climb.h, g100climb.h,
   g200climb.h) textually included here: each defines the hand-off's
   after-routine and act-routine `inline`, which the compiler emits at the
   end of the file in girl_act.h's order, and the mot-routine plainly,
   emitted in place.  Their strings come out here, in this order. */
inline void afterGirlHand50(GObj *volatile self)
{
    debug_StdPrintfDummy("girl after func\n");
    iosOmSendMail(boyGObj, 0x60, isysCurrentGObj);
    ACTGame_DisconnectHand();
}

inline void actGirlHand50(GObj *volatile self)
{
    float dir[4];
    Act *sub = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter actGirlHand50\n");
    sceVu0ScaleVector(dir, (float *)sub->env.wallOrient, -1.0f);
    SetMotionDirection((void *)self, dir);
    SetDirectRootPositionNoFitting((void *)self, (float *)sub->env.wallPos);
    sub->readyFlags = 0;
    sub->after = (void *)afterGirlHand50;
    do {
        _ACTWait(1);
    } while ((sub->readyFlags & 0x10) == 0);
    debug_StdPrintfDummy("girl error flg get\n");
    for (;;) {
        ACTSendMailCorrect((void *)self, 0x60);
        _ACTWait(1);
    }
}

void motGirlHand50(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter motGirlHand50\n");
    while (1) {
        iosOmSendMail(boyGObj, 0x5C, isysCurrentGObj);
        if (sub->readyFlags & 1)
            break;
        _ACTWait(1);
    }
    while (GOBJ_SUB(self)->ctrl.motion < 0x214 || !(GOBJ_SUB(self)->ctrl.motion < 0x21B)) {
        sub->motReq = SetMotionRequest((void *)self, 1, sub->env.motOriReq);
        _ACTWait(1);
    }
    _ACTWait(1);
    while (1) {
        iosOmSendMail(boyGObj, 0x5D, isysCurrentGObj);
        if (sub->readyFlags & 2)
            break;
        _ACTWait(1);
    }
    if ((((int)(sub->flags18.ll >> 44)) & 1) == 0) {
        iosOmSendMail(boyGObj, 0x10, isysCurrentGObj);
        ACTSendMailCorrect((void *)self, 7);
    }
    iosOmSendMail(boyGObj, 0x5E, isysCurrentGObj);
    sub->motReq = SetMotionRequest((void *)self, 0x5E, sub->env.motOriReq);
    while ((((struct MotCtrl *)sub->motReq)->frameEnd & 1) == 0) {
        _ACTWait(1);
    }
    sub->after = 0;
    for (;;) {
        ACTSendMailCorrect((void *)self, 0x47);
        _ACTWait(1);
    }
}

inline void afterGirlHand100(GObj *volatile self)
{
    debug_StdPrintfDummy("girl after func\n");
    iosOmSendMail(boyGObj, 0x65, isysCurrentGObj);
    ACTGame_DisconnectHand();
}

inline void actGirlHand100(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter actGirlHand100\n");
    sub->actMode = 0x53;
    sub->after = (void *)afterGirlHand100;
    sub->readyFlags = 0;
    do {
        _ACTWait(1);
    } while ((sub->readyFlags & 0x10) == 0);
    debug_StdPrintfDummy("girl error flg get\n");
    for (;;) {
        ACTSendMailCorrect((void *)self, 0x65);
        _ACTWait(1);
    }
}

void motGirlHand100(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter motGirlHand100\n");
    while (1) {
        iosOmSendMail(boyGObj, 0x61, isysCurrentGObj);
        if (sub->readyFlags & 1)
            break;
        _ACTWait(1);
    }
    while (GOBJ_SUB(self)->ctrl.motion < 0x214 || !(GOBJ_SUB(self)->ctrl.motion < 0x21B)) {
        sub->motReq = SetMotionRequest((void *)self, 1, sub->env.motOriReq);
        _ACTWait(1);
    }
    _ACTWait(1);
    while (1) {
        iosOmSendMail(boyGObj, 0x62, isysCurrentGObj);
        if (sub->readyFlags & 2)
            break;
        _ACTWait(1);
    }
    iosOmSendMail(boyGObj, 0x63, isysCurrentGObj);
    sub->motReq = SetMotionRequest((void *)self, 0x65, sub->env.motOriReq);
    while (GOBJ_SUB(self)->ctrl.motion < 0x214 || !(GOBJ_SUB(self)->ctrl.motion < 0x21B)) {
        sub->motReq = SetMotionRequest((void *)self, 1, sub->env.motOriReq);
        _ACTWait(1);
    }
    _ACTWait(1);
    while (1) {
        iosOmSendMail(boyGObj, 0x64, isysCurrentGObj);
        if (sub->readyFlags & 8)
            break;
        _ACTWait(1);
    }
    sub->after = 0;
    for (;;) {
        ACTSendMailCorrect((void *)self, 0x47);
        _ACTWait(1);
    }
}

inline void afterGirlHand200(GObj *volatile self)
{
    debug_StdPrintfDummy("girl after func\n");
    iosOmSendMail(boyGObj, 0x6A, isysCurrentGObj);
    ACTGame_DisconnectHand();
}

inline void actGirlHand200(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter actGirlHand200\n");
    sub->actMode = 0x54;
    sub->after = (void *)afterGirlHand200;
    sub->readyFlags = 0;
    do {
        _ACTWait(1);
    } while ((sub->readyFlags & 0x10) == 0);
    debug_StdPrintfDummy("girl error flg get\n");
    for (;;) {
        ACTSendMailCorrect((void *)self, 0x6A);
        _ACTWait(1);
    }
}

void motGirlHand200(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    int n;

    debug_StdPrintfDummy("enter motGirlHand200\n");
    n = (60 - systemStatus[0] * 10) / systemStatus[1] / 2;
    while (1) {
        if (n <= 0)
            goto expired;
        n--;
        iosOmSendMail(boyGObj, 0x66, isysCurrentGObj);
        if (sub->readyFlags & 1)
            break;
        _ACTWait(1);
    }
    goto held;
expired:
    ACTSendMailCorrect((void *)self, 0x6A);
    debug_StdPrintfDummy("%s sync error\n",
                         (void *)self == (void *)((int *)boyGObj) ? "boy" : "girl");
held:
    while (GOBJ_SUB(self)->ctrl.motion < 0x214 || !(GOBJ_SUB(self)->ctrl.motion < 0x21B)) {
        sub->motReq = SetMotionRequest((void *)self, 1, sub->env.motOriReq);
        _ACTWait(1);
    }
    _ACTWait(1);
    while (1) {
        iosOmSendMail(boyGObj, 0x67, isysCurrentGObj);
        if (sub->readyFlags & 2)
            break;
        _ACTWait(1);
    }
    iosOmSendMail(boyGObj, 0x68, isysCurrentGObj);
    sub->motReq = SetMotionRequest((void *)self, 0x66, sub->env.motOriReq);
    while ((((struct MotCtrl *)sub->motReq)->frameEnd & 1) == 0) {
        _ACTWait(1);
    }
    while (1) {
        iosOmSendMail(boyGObj, 0x69, isysCurrentGObj);
        if (sub->readyFlags & 8)
            break;
        _ACTWait(1);
    }
    sub->after = 0;
    for (;;) {
        ACTSendMailCorrect((void *)self, 0x47);
        _ACTWait(1);
    }
}

/* actGirlHand passes two arguments here, so the definition is unprototyped
   and not `(void)`. */
static void GirlBrainClearTarget()
{
    brainClsTargetLevel(&brainGirl);
}

/* The hide/others/listB/listD object lists: 0x30 per entry, 100 entries per
   list, the four lists 0x12D0 apart inside GirlBrainWork (others 0xC90,
   listB 0x1F60, hide 0x3230, listD 0x4500).  The `long long` at 0x08 makes
   the record 8-byte aligned. */
typedef struct { /* field names derived */
    void *obj;   /* 0x00 */
    int pad4;
    long long pad8;
    float pos[4]; /* 0x10 */
    float dist;   /* 0x20 */
    int flags;    /* 0x24 */
    int pad28[2];
} GirlListEnt; /* derived name */

typedef struct { /* field names derived */
    int num;     /* 0x00 */
    char pad4[12];
    GirlListEnt ent[100]; /* 0x10 */
} GirlList;               /* derived name */

typedef struct GirlBrainWork { /* field names derived */
    unsigned char listBNear;   /* 0x00 a listB entry lies within 300 */
    unsigned char listDFound;  /* 0x01 the listD list is not empty */
    char pad2[3214];
    GirlList others;  /* 0x0C90 */
    GirlList listB;   /* 0x1F60 */
    GirlList hide;    /* 0x3230 */
    GirlList listD;   /* 0x4500 */
    void *target;     /* 0x57D0 the brain's current target gobj             */
    void *lastTarget; /* 0x57D4 the target the last DecideMode pass saw      */
    char pad57D8[4];
    int curMode;    /* 0x57DC the mode the brain last switched to */
    int targetFlag; /* 0x57E0 bit 16 of the winning BrainTarget's b18 word */
    char pad57E4[12];
    float runawayGoal[4]; /* 0x57F0 the runaway goal */
    float hidePoint[4];   /* 0x5800 last accepted hide point */
    float runawayFrom[4]; /* 0x5810 where the runaway search starts ("girl brain target") */
    float girlRoot[4];    /* 0x5820 GetRootPosition of the girl */
    float girlPos[4];     /* 0x5830 the girl's own position, GetRootProjectionPosOfGObj */
    float boyRoot[4];     /* 0x5840 GetRootPosition of the boy */
    float boyPos[4];      /* 0x5850 the boy's GetRootProjectionPosOfGObj */
    WayPoint *lastWay;    /* 0x5860, the way point GetWay_next last returned */
    char pad5864[12];
    WVTObj hideWay;            /* 0x5870, the way the girl tries to the hide point */
    unsigned char warn;        /* 0x58F0 a brain state asks the main loop to checkWarning */
    unsigned char pad58F1;     /* 0x58F1 cleared each frame, never read */
    unsigned char modeChanged; /* 0x58F2 set on the frame the mode changes */
    unsigned char lookHold;    /* 0x58F3 3 s after flags18 bit 62 while she faced the boy */
    int markerPulse;           /* 0x58F4 the runaway goal marker's pulse */
    int runMode;               /* 0x58F8 */
    int wait;
    int timer;
    int limit;
    int modeFrames;    /* 0x5908 frames since the last mode change */
    int status31Timer; /* 0x590C frames the attract state keeps char status 31 set */
    int unseenFrames;  /* 0x5910 frames no hide-list object has been in view */
    int hideAdvWait;   /* 0x5914 frames before the hide state may advance */
    int pad5918[2];    /* 0x5918, to the 0x5920 girlBrainMain_Init clears */
} GirlBrainWork;       /* derived name */

/* The head of the TU's .data (the brain work record brain_val and the hand
   manager handmgr follow, defined after the last of these): the
   danger-environment initial value, the brain mode table (the mode's routine
   and its flag byte), the object kinds the others list gathers (-1 ends it)
   and their debug names, the run-mode rows ChangeRunMode indexes, the three
   attract parameter sets, the debug names of the move states, the three
   escape angles and the debug names of the attract states.  Each is defined
   beside its reader: the first three here, the rest further down. */
typedef struct { /* field names derived */
    int kind;
    GObj *obj; /* the danger object, typed like the object globals */
    void *save;
    int count;
} GirlDangerEnv; /* derived name */

typedef struct { /* field names derived */
    void (*proc)(GObj *self);
    unsigned char flag;
} GirlBrainMode; /* derived name */

typedef struct { /* field names derived */
    void *obj;   /* 0x00 the object the girl walks to */
    int pad4[3];
    sceVu0FVECTOR pos;      /* 0x10 */
    int kind;               /* 0x20 copied into the actor's 0x44 when the state ends */
    int mail;               /* 0x24 the mail the state sends when it ends */
    float goalDist;         /* 0x28 the way goal distance the girl arrives within */
    float goalHeight;       /* 0x2C the way goal height she arrives within */
    unsigned char fixedPos; /* 0x30 pos stays as set, not re-read from obj each frame */
    char pad31[15];
    sceVu0FVECTOR dir;      /* 0x40 */
    unsigned char goalTurn; /* 0x50 turn to dir on arrival */
    char pad51[3];
    float slowDist; /* 0x54 the walk slows to half within this goal distance */
    char pad58[8];
} GirlAttractParam; /* derived name */

inline void subGirlBrain_Idle(GObj *volatile self);
void subGirlBrain_Attract(GObj *volatile self);
void subGirlBrain_Escape(GObj *volatile self);
void subGirlBrain_Hide(GObj *volatile self);
inline void subGirlBrain_Hesitate(GObj *volatile self);
inline void subGirlBrain_Becarry(GObj *volatile self);
inline void subGirlBrain_Busy(GObj *volatile self);
void subGirlBrain_Pulledup(GObj *volatile self);
inline void subGirlBrain_DangerEnv(GObj *volatile self);
void subGirlBrain_HideAdvance(GObj *volatile self);

static GirlDangerEnv dangerEnvDefault = {0}; /* derived name */

static GirlBrainMode girlBrainModeTable[10] = {
    /* derived name */
    {subGirlBrain_Idle, 0},        {subGirlBrain_Attract, 0},  {subGirlBrain_Escape, 1},
    {subGirlBrain_Hide, 1},        {subGirlBrain_Hesitate, 1}, {subGirlBrain_Becarry, 0},
    {subGirlBrain_Busy, 0},        {subGirlBrain_Pulledup, 0}, {subGirlBrain_DangerEnv, 0},
    {subGirlBrain_HideAdvance, 1},
}; /* derived name */

static int othersKindList[3] = {4, 62, -1}; /* derived name */

/* The TU's .bss: sort_list's index/distance pairs and its copy of the sorted
   positions, CorrectList's compaction scratch and the runaway candidate list
   (ten positions each), subGirlBrain_Escape's debug string buffer,
   subGirlCollision's direction request (priority, direction, the vector at
   +0x10) and the current danger environment (subGirlBrainMain copies
   dangerEnvDefault into it and the Danger_* routines read its object). */
typedef struct { /* field names derived */
    int idx;
    float dist;
} GirlSortEnt; /* derived name */

static GirlSortEnt sortList[100]; /* derived name */

static float sortPos[100][4]; /* derived name */

static float correctListWork[10][4]; /* derived name */

static float runawayPointList[10][4]; /* derived name */

static char escapeDebugString[512]; /* derived name */

typedef struct {       /* field names derived */
    int prio;          /* 0x00 */
    int dir;           /* 0x04 */
    sceVu0FVECTOR vec; /* 0x10 */
} GoalTurnReq;         /* derived name */

static GoalTurnReq goalTurnReq; /* derived name */

static GirlDangerEnv dangerEnv; /* derived name */

inline void ACTGame_GirlBeforeFunc(GObj *self)
{
    float boyPos[4];
    float girlPos[4];
    Act *s;

    s = GOBJ_ACT(self);
    goalTurnReq.prio = 0;
    goalTurnReq.dir = 0;
    if (s->actMode != 69) {
        s->flags20.ll &= ~0x100000ULL;
    }
    s->flags18.ll &= ~0x40000000000ULL;
    if (ACTGame_FLAG_TETSUNAGI()) {
        if (s->actMode == 5 || s->actMode == 69) {
            s->flags18.ll |= 0x40000000000ULL;
        } else {
            GetSkeltonPosition(boyPos, (boyGObj), 6);
            GetSkeltonPosition(girlPos, girlGObj, 22);
            if (_DistSqGV(boyPos, girlPos) < 900.0f) {
                s->flags18.ll |= 0x40000000000ULL;
            }
        }
    }
}

/* Defined before the girl_brain_main.c.inc run; subGirlBrain_Pulledup and
   subGirlCollision expand it.  The first parameter is unused. */
static inline void ATGoalTurnSet(void *actor, int prio, int dir, float *v) /* derived name */
{
    if (prio >= goalTurnReq.prio) {
        goalTurnReq.prio = prio;
        goalTurnReq.dir = dir;
        goalTurnReq.vec[0] = v[0];
        goalTurnReq.vec[1] = v[1];
        goalTurnReq.vec[2] = v[2];
    }
}

/* The second static inline of the pair: subGirlCollision expands it once.  It
   reads back the turn request ATGoalTurnSet writes and answers it with the
   turn mail for the request's priority. */
static inline void ATGoalTurnSendMail(GObj *self) /* derived name */
{
    Act *act = GOBJ_ACT(self);
    int mail = -1;
    int slow[2] = {231, 242};
    int fast[2] = {232, 243};
    int idx = 0;

    switch (goalTurnReq.prio) {
    case 2:
    case 3:
        idx = 1;
        break;
    }
    switch (goalTurnReq.dir) {
    case 1:
        mail = slow[idx];
        break;
    case 2:
        mail = fast[idx];
        break;
    }
    if (mail > 0) {
        act->env.turnDir.f[0] = goalTurnReq.vec[0];
        act->env.turnDir.f[1] = goalTurnReq.vec[1];
        act->env.turnDir.f[2] = goalTurnReq.vec[2];
        ACTSendMailCorrect(self, mail);
    }
}

/* The wire-string marker (colour, a MatrixDrive transform of the position,
   DispWireString, colour reset), built only under DEBUG, as
   girlBrainDebugPrint is. */
static inline void girlDispWire(int r, int g, int b, float *pos, char *str) /* derived name */
{
#ifdef DEBUG
    float mtx[16];

    ChangeColorWireString(r, g, b);
    MatrixDrive_PushMatrix();
    sceVu0TransposeMatrix(mtx, ((float *)((float *)matrixptr)) + 32);
    mtx[3] = mtx[7] = mtx[11] = 0.0f;
    sceVu0UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_TransMatrix(pos[0], pos[1], pos[2]);
    sceVu0MulMatrix(MatrixDrive_GetMatrix(), MatrixDrive_GetMatrix(), mtx);
    MatrixDrive_PushMatrix();
    MatrixDrive_TransMatrix(0.0f, -50.0f, 0.0f);
    DispWireString(str);
    MatrixDrive_PopMatrix();
    MatrixDrive_PopMatrix();
    DefaultColorWireString();
#endif
}

/* the marker colour for a hide point; the caller supplies the marker text */
static inline void girlDispHidePoint(float *pos, int c, char *str) /* derived name */
{
    int r = 0;
    int g = 0;
    int b = 0;

    switch (c) {
    case 'R':
        r = 255;
        break;
    case 'B':
        b = 255;
        break;
    case 'W':
        r = 255;
        g = 255;
        b = 255;
        break;
    case 'Y':
        g = 255;
        b = 255;
        break;
    }
    girlDispWire(r, g, b, pos, str);
}

inline void SetGirlDangerGObj(GObj *self)
{
    GObj *g = girlGObj;
    if (g != 0) {
        GOBJ_WORK(g)->dangerObj = self;
    }
}

inline void ClearGirlDangerGObj(void)
{
    char *g = (char *)girlGObj;
    if (g != 0) {
        GOBJ_WORK(g)->dangerObj = 0;
    }
}

static void SetTurnSpeedInEscape(GObj *self)
{
    if (GOBJ_ACT(self)->actMode == 10) {
        ACTGame_SetMotionPlaySpeedRatio_Reserve(self, 1.5f, 5);
    }
}

static inline int girlListIsOnBoy(void *gobj) /* derived name */
{
    if (((GObj *)gobj)->kind != 4) {
        return 1;
    }
    return EnemyBrainStatus_Boy(gobj);
}

static inline int girlListIsAlive(void *gobj) /* derived name */
{
    return (int)(GOBJ_ACT(gobj)->flags18.ll >> 32) & 1;
}

static inline int enemy_list_compare(const void *p, const void *q)
{
    float diff = ((GirlListEnt *)p)->dist - ((GirlListEnt *)q)->dist;
    return (int)diff;
}

/* the flag-masked record copy the three sub-lists share */
static inline int girlListPick(GirlListEnt *src, GirlListEnt *dst, int n,
                               int mask) /* derived name */
{
    int cnt = 0;
    int i;

    for (i = 0; i < n; i++) {
        if (src[i].flags & mask) {
            dst[cnt] = src[i];
            cnt++;
        }
    }
    return cnt;
}

static void sort_list(float *list, int n)
{
    GirlSortEnt t;
    int i;
    int j;

    for (i = 0; i < n; i++) {
        sortList[i].idx = i;
        sortList[i].dist = _DistSqGV(list + i * 4, brain_val.girlRoot);
    }
    for (i = 0; i < n; i++) {
        for (j = n - 1; i < j; j--) {
            if (sortList[j].dist < sortList[j - 1].dist) {
                t = sortList[j];
                sortList[j] = sortList[j - 1];
                sortList[j - 1] = t;
            }
        }
    }
    for (i = 0; i < n; i++) {
        float *d = sortPos[i];

        d[0] = list[i * 4 + 0];
        d[1] = list[i * 4 + 1];
        d[2] = list[i * 4 + 2];
    }
    for (i = 0; i < n; i++) {
        float *e = sortPos[sortList[i].idx];

        list[i * 4 + 0] = e[0];
        list[i * 4 + 1] = e[1];
        list[i * 4 + 2] = e[2];
    }
}

static void girlBrainMain_MakeOthersList(void)
{
    /* a GNU nested function */
    float d;
    int seen;
    int i;
    int j;
    int k;

    brain_val.hide.num = brain_val.listB.num = 0;
    for (i = 0; othersKindList[i] != -1; i++) {
        void *o;

        for (o = isysGObjSearchFromObjKindID_begin(othersKindList[i]); o != 0;
             o = isysGObjSearchFromObjKindID_next(o)) {
            if (girlListIsAlive(o) && girlListIsOnBoy(o)) {
                if (!(brain_val.hide.num < 100)) {
                    /* "too many enemies", EUC-JP */
                    debug_StdPrintfDummy(
                        "\305\250\244\316\277\364\244\254\302\277\244\271\244\256\244\336\244\271");
                    debug_assert("src/girl_brain_main.c.inc", 485);
                    __assert("src/girl_brain_main.c.inc", 485, "0");
                }
                GetRootPosition(brain_val.hide.ent[brain_val.hide.num].pos, o);
                brain_val.hide.num++;
            }
        }
    }
    sort_list(brain_val.listB.ent[0].pos, brain_val.listB.num);
    sort_list(brain_val.hide.ent[0].pos, brain_val.hide.num);
    brain_val.others.num = 0;
    for (i = 0; othersKindList[i] != -1; i++) {
        void *o;

        for (o = isysGObjSearchFromObjKindID_begin(othersKindList[i]); o != 0;
             o = isysGObjSearchFromObjKindID_next(o)) {
            if (girlListIsAlive(o)) {
                int n = brain_val.others.num;

                brain_val.others.ent[n].obj = o;
                GetRootProjectionPosOfGObj(brain_val.others.ent[n].pos, o);
                brain_val.others.ent[n].dist =
                    _DistGV(brain_val.others.ent[n].pos, brain_val.girlPos);
                brain_val.others.ent[n].flags = 1;
                if (girlListIsOnBoy(o)) {
                    brain_val.others.ent[n].flags |= 2;
                }
                brain_val.others.num++;
            }
        }
    }
    qsort(brain_val.others.ent, brain_val.others.num, sizeof(GirlListEnt), enemy_list_compare);
    brain_val.listB.num =
        girlListPick(brain_val.others.ent, brain_val.listB.ent, brain_val.others.num, 0xC);
    brain_val.hide.num =
        girlListPick(brain_val.others.ent, brain_val.hide.ent, brain_val.others.num, 0xF);
    brain_val.listD.num =
        girlListPick(brain_val.others.ent, brain_val.listD.ent, brain_val.others.num, 0xE);
    brain_val.listBNear = 0;
    if (brain_val.listB.num != 0 && brain_val.listB.ent[0].dist < 300.0f) {
        brain_val.listBNear = 1;
    }
    brain_val.listDFound = 0;
    if (brain_val.listD.num != 0) {
        brain_val.listDFound = 1;
    }
    /* a marker at each hide point, coloured by the others entry's flags;
       only its drawing is compiled out (see girlDispWire), so the loop stays
       and counts down empty */
    for (k = 0; k < brain_val.hide.num; k++) {
        int c = brain_val.others.ent[k].flags & 2 ? 'B' : 'W';

        if (brain_val.others.ent[k].flags & 4) {
            c = 'Y';
        }
        if (brain_val.others.ent[k].flags & 8) {
            c = 'R';
        }
        girlDispHidePoint(brain_val.hide.ent[k].pos, c, "III");
    }
    seen = 0;
    for (i = 0; i < brain_val.hide.num; i++) {
        if (ACTGameView_Check(girlGObj, brain_val.hide.ent[i].obj) != 0) {
            seen = 1;
            break;
        }
    }
    if (seen != 0) {
        brain_val.unseenFrames = 0;
    } else {
        brain_val.unseenFrames++;
    }
    if (brain_val.others.num != 0 && brain_val.others.ent[0].dist < 600.0f) {
        GOBJ_ACT(girlGObj)->flags20.ll |= 0x1000;
    }
    GOBJ_ACT(girlGObj)->flags20.ll &= ~0x2000;
    if (brain_val.others.num != 0 && brain_val.others.ent[0].dist < 1000.0f) {
        GOBJ_ACT(girlGObj)->flags20.ll |= 0x2000;
    }
    GOBJ_ACT(girlGObj)->flags20.ll |= 0x400000000000;
    if (brain_val.others.num != 0) {
        if (brain_val.others.ent[0].dist < 200.0f) {
            GOBJ_ACT(girlGObj)->flags20.ll &= ~0x400000000000;
            return;
        }
        d = _DistGV(brain_val.girlPos, brain_val.boyPos);
        for (j = 0; j < brain_val.others.num; j++) {
            if (_DistSqGV(brain_val.others.ent[j].pos, brain_val.boyPos) < d * d) {
                GOBJ_ACT(girlGObj)->flags20.ll &= ~0x400000000000;
                break;
            }
        }
    }
}

/* The group names, defined here after the list builder. */
static char *groupRelationName[4] = {"FALSE", "OTHERGROUP", "SAMEGROUP",
                                     "DIRECT"}; /* derived name */

static int girlBrainHideCheckIntercept(float *from, float *to, GirlListEnt *list, int n)
{
    float d[4];
    float m[16];
    float v[4];
    int i;
    float dist;
    float flag;
    float dy;

    sceVu0SubVector(d, from, to);
    d[1] = 0.0f;
    GetMatrixDirectionToZ(m, d);
    dist = FSqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    for (i = 0; i < n; i++) {
        flag = GOBJ_ACT(list[i].obj)->actMode == 6 ? 1.0f : 0.0f;
        dy = list[i].pos[1] - from[1] < 0.0f ? -(list[i].pos[1] - from[1])
                                             : list[i].pos[1] - from[1];

        if (flag != 0.0f) {
            if (80.0f < dy) {
                continue;
            }
        } else {
            if (200.0f < dy) {
                continue;
            }
        }
        sceVu0SubVector(v, list[i].pos, to);
        v[1] = 0.0f;
        v[3] = 0.0f;
        sceVu0ApplyMatrix(v, m, v);

        if (0.0f < v[2]) {
            if (v[2] < dist) {
                if (v[0] * v[0] + v[1] * v[1] < 10000.0f) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

/* a static inline */
static inline void dispWayMarker(float *p) /* derived name */
{
    float buf[4];
    sceVu0ScaleVector(buf, p, -1.0f);
    debug_Marker(buf, 0xFF, 0, 0, 70.0f, 0.0f);
}

/* A static inline, inlined at two sites: true when the candidate hide point
   sits above the girl's floor by more than 100 units. */
static inline unsigned char isHidePointTooHigh(float *p) /* derived name */
{
    GirlBrainWork *b;
    float y;

    if (stage_no == 8 || stage_no == 22) {
        b = &brain_val;
        y = b->girlPos[1] + 100.0f;
        if (y < p[1] || y < b->boyPos[1]) {
            return 1;
        }
    }
    return 0;
}

/* One call site.  `r = 0;` is a statement after the struct copy, not an
   initialiser. */
static inline int girlBrainHide_TryWay(float *pt, WVTObj *way, float *goal,
                                       float *hit) /* derived name */
{
    float start[4];
    int r = 0;
    void *girl = boyGObj;
    void *self = girlGObj;
    Act *sub = GOBJ_ACT(self);

    *way = sub->way;
    if (GetWay_begin(pt, way, goal)) {
        r = 1;
        start[0] = pt[0];
        start[1] = pt[1];
        start[2] = pt[2];
        start[1] -= 50.0f;
        goal[1] = goal[1] - 30.0f;
        way->reached = 0;
        if (ACTCheckCollis_WAY(10.0f, goal, start, girl, hit) == 0) {
            way->reached = 1;
            DeleteGuideWay(way);
            r = 3;
        } else if (way->flag3C == 0) {
            r = 2;
        }
    }
    return r;
}

static int girlBrainMain_CheckWarningMode(unsigned char check)
{
    float hit[4];
    int mode;

    brain_val.pad58F1 = 1;
    if (brain_val.listBNear != 0) {
        mode = 2;
    } else {
        mode = brain_val.listDFound != 0 ? 4 : 0;
    }
    {
        if (brain_val.hide.num == 0 || (check != 0 && ACTGameView_Check(girlGObj, boyGObj) == 0)) {
            goto out;
        }
        _girlBrainHide_MakeHidePoint(brain_val.hidePoint, 200.0f);
        dispWayMarker(brain_val.hidePoint);
        if (girlBrainHide_TryWay(brain_val.hidePoint, &brain_val.hideWay, brain_val.girlPos, hit) !=
            3) {
            goto out;
        }
    }
    if (!isHidePointTooHigh(brain_val.hidePoint) &&
        !girlBrainHideCheckIntercept(brain_val.girlPos, brain_val.hidePoint, brain_val.hide.ent,
                                     brain_val.hide.num)) {
        mode = 3;
    } else {
        if (brain_val.listB.num != 0 &&
            _DistSqGV(brain_val.girlRoot, brain_val.listB.ent[0].pos) < 90000.0f) {
            goto out;
        }
        mode = 4;
    }
out:
    return mode;
}

/* the pad record: the button word at +0 */

/* the brain target pass, inlined into girlBrainMain_DecideMode and into
   subGirlBrainMain */
static inline void *girlBrainGetTarget(void) /* derived name */
{
    int *flag = &brain_val.targetFlag;
    Brain *b = &brainGirl;
    void *obj = 0;

    brainGetTarget(b);
    *flag = 0;
    if (b->idx != -1) {
        obj = (void *)b->tgt[b->idx].gobj;
        *flag = *(unsigned short *)&b->tgt[b->idx].detail & 1;
    }
    return obj;
}

/* The girl brain's TTY trace of its escape target, built only when DEBUG is
   defined; the retail build leaves the helper without a body.
   subGirlBrain_Escape's four calls are commented at that function. */
static __inline__ void girlBrainDebugPrint(void) /* derived name */
{
#ifdef DEBUG
    float *t = brain_val.runawayFrom;

    scePrintf("girl brain target %f %f %f\n", t[0], t[1], t[2]);
#endif
}

static inline float girlBrainGetTargetLevel(void) /* derived name */
{
    if (brainGirl.idx == -1) {
        return 0.0f;
    }
    return brainGirl.targetLevel;
}

/* the kind of the held target, 0 while none is held; the actor parameter is
   not read */
static inline int girlBrainGetTargetType(void *g) /* derived name */
{
    Brain *b = &brainGirl;

    if (b->idx == -1) {
        return 0;
    }
    return (unsigned short)b->targetType;
}

/* the walk ratio that goes with a target kind */
static inline float girlBrainGetTypeRatio(int type) /* derived name */
{
    float r;

    switch (type) {
    case 2:
        r = 0.5f;
        break;
    case 3:
        r = 1.0f;
        break;
    case 1:
    default:
        r = 0.0f;
        break;
    }
    return r;
}

static __inline void setNext(int *next, int m)
{
    if (girlControlMode != 0 && girlBrainModeTable[m].flag != 0) {
        return;
    }
    *next = m;
}

static __inline void checkWarning(int *next, unsigned char c)
{
    int m = girlBrainMain_CheckWarningMode(c);

    if (0 <= m) {
        setNext(next, m);
    }
}

static int girlBrainMain_DecideMode(int mode, int *next)
{
    /* two nested helpers, reading `next` through the static chain */
    float gpos[4];
    float opos[4];
    void *o;
    float lv;
    int seen;
    int i;
    void *self = girlGObj;
    Act *sub = GOBJ_ACT(self);
    int warned = 0;
    int changed = 0;
    int near = 0;

    gpos[0] = test_CURRENTROOT(self)[0];
    gpos[1] = test_CURRENTROOT(self)[1];
    gpos[2] = test_CURRENTROOT(self)[2];

    if (sub->actMode == 0x6F) {
        setNext(next, 5);
        return 0;
    }
    brain_val.target = girlBrainGetTarget();

    lv = girlBrainGetTargetLevel() * 10.0f;
    if (3.0f <= lv) {
        sub->flags20.ll |= 0x100000000000;
    }
    if (2.0f <= lv) {
        sub->flags20.ll |= 0x200000000000;
    }
    if (brain_val.target != brain_val.lastTarget) {
        brain_val.lastTarget = brain_val.target;
        changed = 1;
    }
    if (brain_val.target != 0) {
        _ACTCharStatus_Set(self, 10, -1.0f, (ICO_WORD)brain_val.target);
    }
    switch (mode) {
    case 0:
    case 1:
    case 7:
        if (brain_val.lastTarget != 0) {
            setNext(next, 1);
        } else {
            setNext(next, 0);
        }
        if (brain_val.hide.num == 0) {
            break;
        }
        for (i = 0; othersKindList[i] != -1; i++) {
            for (o = isysGObjSearchFromObjKindID_begin(othersKindList[i]); o != 0;
                 o = isysGObjSearchFromObjKindID_next(o)) {
                seen = 0;
                opos[0] = test_CURRENTROOT(o)[0];
                opos[1] = test_CURRENTROOT(o)[1];
                opos[2] = test_CURRENTROOT(o)[2];
                if (ACTCheckView(self, o, opos, 160, 0.0f) != 0 &&
                    _DistSqGV(gpos, opos) < 160000.0f) {
                    seen = 1;
                }
                if (ACTGameView_Check(self, o) != 0 || seen != 0) {
                    checkWarning(next, 0);
                    break;
                }
            }
        }
        break;

    case 2:
        if (brain_val.warn != 0) {
            checkWarning(next, 1);
            brain_val.warn = 0;
        }
        if (pad[0].now & 8) {
            ACTSendMailCorrect(self, 251);
        }
        break;

    case 3:
        if (brain_val.hideAdvWait == 0 && isEnterHideadv() != 0) {
            setNext(next, 9);
            break;
        }
        if ((60 - systemStatus[0] * 10) / systemStatus[1] * 10 < brain_val.unseenFrames &&
            (60 - systemStatus[0] * 10) / systemStatus[1] * 10 < brain_val.modeFrames) {
            setNext(next, 0);
            break;
        }
        /* fall through */

    case 4:
        if (brain_val.warn != 0) {
            checkWarning(next, 0);

            warned = 1;
            brain_val.warn = 0;
        }
        break;

    case 5:
        setNext(next, 6);
        break;

    case 6:
        if (brain_val.warn == 0) {
            break;
        }
        /* fall through */

    case 8:
        setNext(next, 0);
        brain_val.warn = 0;
        break;

    case 9:
        if (isEnterHideadv() != 0) {
            break;
        }
        setNext(next, 3);
        break;
    }
    if (PAIR_IsStatus_BOY_WAIT() != 0 &&
        _DistxzSqGV(test_CURRENTROOT((boyGObj)), test_CURRENTROOT((void *)girlGObj)) < 40000.0f) {
        near = 1;
        girlBrainDebugPrint();
    }
    if (*next != 2 && PAIR_IsStatus_BOY_WAIT() != 0) {
        if (*next != 4 || near != 0) {
            setNext(next, 7);
            return 0;
        }
    }
    return *next == 1 ? changed : warned;
}

static void girlBrainMain_PositionUpdate(void)
{
    GetRootPosition(brain_val.girlRoot, (void *)girlGObj);
    GetRootPosition(brain_val.boyRoot, boyGObj);
    GetRootProjectionPosOfGObj(brain_val.girlPos, (void *)girlGObj);
    GetRootProjectionPosOfGObj(brain_val.boyPos, boyGObj);
}

static void girlBrainMain_Init(void)
{
    memset(&brain_val, 0, sizeof(brain_val));
}

/* the run-mode rows ChangeRunMode indexes */
static int runModeTable[4][4] = {
    /* derived name */
    {0, 0, 0, 0},
    {1, 1, 0, 0},
    {2, 1, 1, 0},
    {3, 2, 1, 1},
}; /* derived name */

/* param-escape-run as the bytes of its [t][mode] grid: girl_act.h's
   EscapeRange rows read t * 16 + mode * 8 bytes in */
extern char paramEscapeRun[];
/* motionOrientManager.h declares none of the motion tables */
extern MotionDef motionKind[];

/* declared in girl_act.h: the girl's look timer and its state, a tentative
   definition */
int GirlInfo[2];

/* A file-scope static helper, inlined in Danger_Box, its nested
 * GetSafePosition and subGirlBrainMain: whether the boy is pushing a
 * truck-type box (his action mode is 49 and the box he holds is box kind
 * 7). */
static inline unsigned char isBoyPushBoxTruck(void) /* derived name */
{
    Act *sub;
    GObj *box;

    if (((int *)boyGObj) != 0 && (sub = GOBJ_ACT(((int *)boyGObj)))->actMode == 49 &&
        (box = sub->box) != 0 && IsThisBoxTruck(box) == 7) {
        return 1;
    }
    return 0;
}

static void ChangeRunMode(int mode)
{
    int n;
    int t;
    int lo;
    int hi;

    brain_val.runMode = mode;
    brain_val.timer = 0;
    brain_val.wait = rand() % 3;
    n = (int)brainGirl.threshold;
    n = n / 3;
    n = (n < 0) ? 0 : ((n < 4) ? n : 3);
    t = runModeTable[n][0];
    lo = *(int *)(t * 16 + mode * 8 + paramEscapeRun);
    hi = *(int *)(paramEscapeRun + (t * 16 + mode * 8) + 4);
    brain_val.limit = lo + rand() % (hi - lo);
}

/* The actor entry parameter is volatile: the actor scheduler writes the
   caller's home slot while this thread is parked in _ACTWait. */
void subGirlBrainMain(GObj *volatile self)
{
    /* ChangeRunMode, a GNU nested function */
    float sk[4];
    Act *act = GOBJ_ACT(self);
    int mode = 0;
    int prevMode = -1;
    int turned;
    int look;
    int fire;
    int hold = 0;
    int mark = 0;
    int span = (60 - systemStatus[0] * 10) / systemStatus[1] * 3;

    girlBrainMain_Init();

    dangerEnv = dangerEnvDefault;

    ChangeRunMode(0);
    for (;;) {
        int carry = act->actMode == 0x45;

        act->flags20.ll = (act->flags20.ll & ~(1LL << 27)) | ((long long)carry << 27);
        if (brain_val.hideAdvWait) {
            brain_val.hideAdvWait -= 1;
        }
        if (((int *)boyGObj)) {
            unsigned int st = GOBJ_ACT(((int *)boyGObj))->actMode;

            if (st == 7) {
                _ACTCharStatus_Set((void *)self, 0, -1.0f, 0);
            } else if (st >= 7) {
                if (st < 23) {
                    if (st >= 21) {
                        _ACTCharStatus_Set((void *)self, 1, -1.0f, 0);
                    }
                }
            }
        }
        if (((int)(act->flags20.ll >> 8)) & 1) {
            brain_val.status31Timer = (60 - systemStatus[0] * 10) / systemStatus[1] * 100 / 60;
        }
        if (brain_val.status31Timer > 0) {
            brain_val.status31Timer -= 1;
        }
        turned = 0;
        {
            float dir[4];
            float pos[4];

            if (brain_val.lastTarget == (void *)((int *)boyGObj)) {
                GetSkeltonOrient(sk, (void *)self, 35);
                GetSkeltonPosition(pos, (void *)self, 35);
                _OrientGV(dir, test_CURRENTROOT((boyGObj)), pos);
                turned = _RotGV(sk, dir) < 20;
            }
        }
        if (mark > 0) {
            mark -= 1;
        } else if (((int)(((ActStatus *)&act->flags18.ll)->ll >> 62)) & 1) {
            mark = span;
            if (turned) {
                hold = span;
            }
        }
        if (hold > 0) {
            brain_val.lookHold = 1;
            hold -= 1;
        } else {
            brain_val.lookHold = 0;
        }
        ((ActStatus *)&act->flags18.ll)->ll &= ~(1LL << 54);
        ACTGameView_Loop((void *)self);
        brain_val.pad58F1 = 0;
        brain_val.modeChanged = 0;
        girlBrainMain_PositionUpdate();
        girlBrainMain_MakeOthersList();
        {
            void *boy = girlGObj;
            Act *bact = GOBJ_ACT(boy);
            int near;

            if (brain_val.hide.num) {
                if (brain_val.hide.ent[0].dist < 200.0f) {
                    _ACTCharStatus_Set(boy, 4, -1.0f, 0);
                }
                if (bact->frame % ((60 - systemStatus[0] * 10) / systemStatus[1]) == 0) {
                    GOBJ_WORK(boy)->hideObj = brain_val.hide.ent[0].obj;
                }
                if (GOBJ_WORK(boy)->hideObj == 0) {
                    GOBJ_WORK(boy)->hideObj = brain_val.hide.ent[0].obj;
                }
                ICO_RAW(void *, bact, 0x80, bact->gobj80) =
                    ICO_RAW(void *, GOBJ_ACT(boy)->work, 0x370, GOBJ_WORK(boy)->hideObj);
                bact->gobj84 = ICO_RAW(void *, GOBJ_ACT(boy)->work, 0x370, GOBJ_WORK(boy)->hideObj);
            }
            if (brain_val.hide.num != 0 || SearchActiveGenerator() != 0) {
                near = 1;
            } else {
                near = 0;
            }
            if ((((int)(act->flags20.ll >> 28)) & 1) && !near) {
                GOBJ_WORK(self)->brainTimer =
                    (int)(_ACTGame_GetParamF(0) *
                          (float)((60 - systemStatus[0] * 10) / systemStatus[1]));
            }
            act->flags20.ll = (act->flags20.ll & ~(1LL << 28)) | ((long long)near << 28);
        }
        if (GOBJ_WORK(self)->brainTimer != 0) {
            float lv;

            GOBJ_WORK(self)->brainTimer -= 1;
            lv = (float)GOBJ_WORK(self)->brainTimer * 10.0f /
                 (_ACTGame_GetParamF(0) * (float)((60 - systemStatus[0] * 10) / systemStatus[1]));
            if (lv > 2.0f) {
                brainSetLevelGop(boyGObj, lv, 0, 1);
            } else {
                GOBJ_WORK(self)->brainTimer = 0;
            }
        }
        {
            GObj *o;

            for (o = isysGObjGetExist_begin(); o != 0; o = isysGObjGetExist_next(o)) {
                switch (o->kind) {
                case 1:
                case 4:
                case 17:
                    GetRootPosition(sk, o);
                    if (ACTGameView_Check(self, o)) {
                        girlDispWire(255, 0, 0, sk, "X");
                    } else {
                        girlDispWire(0, 0, 255, sk, "X");
                    }
                    break;
                }
            }
        }
        {
            float dir[4];
            float pos[4];
            float far[4];
            float ofs[4];
            float eye[4];

            if (((int)(((ActStatus *)&act->flags18.ll)->ll >> 49)) & 1) {
                if (girlBrainMain_DecideMode(prevMode, &mode)) {
                    prevMode = -1;
                }
            } else {
                mode = 0;
            }
            {
                GObj *o;

                for (o = isysGObjSearchFromObjKindID_begin(19); o != 0;
                     o = isysGObjSearchFromObjKindID_next(o)) {
                    if (o->active) {
                        void *torch = GetBombTorchGObj(o);

                        if (torch && IsTorchLightOn(torch)) {
                            mode = 8;
                            dangerEnv.obj = o;
                            dangerEnv.kind = 1;
                        }
                    }
                }
            }
            {
                void *tgt = (void *)GOBJ_WORK(self)->dangerObj;

                if (tgt) {
                    dir[0] = test_CURRENTROOT(tgt)[0];
                    dir[1] = test_CURRENTROOT(tgt)[1];
                    dir[2] = test_CURRENTROOT(tgt)[2];
                    pos[0] = test_CURRENTROOT((void *)self)[0];
                    pos[1] = test_CURRENTROOT((void *)self)[1];
                    pos[2] = test_CURRENTROOT((void *)self)[2];
                    if (_DistxzSqGV(dir, pos) < 250000.0f &&
                        (dir[1] - pos[1] < 0.0f ? -(dir[1] - pos[1]) : dir[1] - pos[1]) < 1000.0f &&
                        (dir[1] - pos[1] < 0.0f ? -(dir[1] - pos[1]) : dir[1] - pos[1]) > 100.0f) {
                        mode = 8;
                        dangerEnv.obj = tgt;
                        dangerEnv.kind = 2;
                    }
                }
            }
            {
                int hit = 0;
                int clear = 0;

                if (((int *)boyGObj) && GOBJ_ACT(((int *)boyGObj))->actMode == 49 &&
                    GOBJ_ACT(((int *)boyGObj))->box) {
                    float rad;

                    rad = isBoyPushBoxTruck() ? 300.0f : 150.0f;
                    eye[0] = test_CURRENTORIENT((boyGObj))[0];
                    eye[1] = test_CURRENTORIENT((boyGObj))[1];
                    eye[2] = test_CURRENTORIENT((boyGObj))[2];
                    pos[0] = test_CURRENTROOT(GOBJ_ACT(((int *)boyGObj))->box)[0];
                    pos[1] = test_CURRENTROOT(GOBJ_ACT(((int *)boyGObj))->box)[1];
                    pos[2] = test_CURRENTROOT(GOBJ_ACT(((int *)boyGObj))->box)[2];
                    dir[0] = test_CURRENTROOT((void *)self)[0];
                    dir[1] = test_CURRENTROOT((void *)self)[1];
                    dir[2] = test_CURRENTROOT((void *)self)[2];
                    sceVu0ScaleVector(ofs, eye, 100.0f);
                    sceVu0AddVector(far, pos, ofs);
                    if (_DistxzSqGV(far, dir) < rad * rad) {
                        hit = 1;
                    }
                    sceVu0ScaleVector(ofs, eye, -100.0f);
                    sceVu0AddVector(far, pos, ofs);
                    if (_DistxzSqGV(far, dir) < 22500.0f) {
                        hit = 1;
                    }
                    if (!(_DistxzSqGV(pos, dir) < 160000.0f)) {
                        hit = 0;
                    }
                } else {
                    clear = 1;
                }
                if (clear) {
                    dangerEnv.count = 0;
                }
                if (hit) {
                    dangerEnv.save = GOBJ_ACT(((int *)boyGObj))->box;
                    dangerEnv.count = (60 - systemStatus[0] * 10) / systemStatus[1] * 5;
                }
            }
            if (dangerEnv.count) {
                dangerEnv.count -= 1;
                mode = 8;
                dangerEnv.obj = dangerEnv.save;
                dangerEnv.kind = 3;
            }
        }
        {
            int tbl[5][2] = {{1633, 1634}, {1631, 1632}, {2024, 2025}, {2022, 2023}, {-1, 0}};
            GObj *ent;
            void *g;
            int i;

            ent = GOBJ_SUB(self)->parent.obj;
            if (ent && ((int *)boyGObj) && GOBJ_ACT(((int *)boyGObj))->actMode == 51) {
                for (i = 0; tbl[i][0] != -1; i++) {
                    if (ent->labelId == tbl[i][0]) {
                        g = isysGObjSearchFromObjLayoutID(tbl[i][1]);
                        if (g) {
                            mode = 8;
                            dangerEnv.obj = g;
                            dangerEnv.kind = 4;
                        }
                        break;
                    }
                }
            }
        }
        if (((int *)boyGObj) && GOBJ_ACT(((int *)boyGObj))->actMode == 52 &&
            FloorIsTruck(boyGObj) && FloorIsTruck((void *)self)) {
            mode = 0;
            act->flags20.ll |= 0x8000000;
        }
        if (mode != prevMode) {
            brain_val.modeFrames = 0;
            actChangeActBrain(isysCurrentGObj, girlBrainModeTable[mode].proc, &act->brainProc);
            prevMode = mode;
            brain_val.modeChanged = 1;
            brain_val.curMode = mode;
            switch (mode) {
            case 0:
                act->infoPos = 0;
                break;
            case 1:
                act->infoPos = 1;
                break;
            case 2:
            case 3:
            case 4:
            case 6:
            case 9:
                act->infoPos = 2;
                break;
            case 5:
                act->infoPos = 3;
                break;
            case 7:
            case 8:
                break;
            }
        }
        switch (mode) {
        case 0:
        case 1:
            brainGirl.minThreshold = 0.0f;
            break;
        case 4:
            if ((((int)(((ActStatus *)&act->flags18.ll)->ll >> 63)) & 1) ||
                (act->flags20.i[0] & 1)) {
                act->flags20.ll |= 4;
            }
            ACTSendMailCorrect((void *)self, 0x155);
            brainGirl.minThreshold = 3.0f;
            if (brain_val.timer++ >
                brain_val.limit * ((60 - systemStatus[0] * 10) / systemStatus[1]) / 60) {
                ChangeRunMode(brain_val.runMode == 0);
            }
            _ACTParaStatus_Set((void *)self, brain_val.wait + 11);
            _ACTCharStatus_Set((void *)self, 8, 0.0f, 0);
            SetTurnSpeedInEscape(self);
            break;
        case 2:
            ACTSendMailCorrect((void *)self, 0x155);
            brainGirl.minThreshold = 6.0f;
            if (brain_val.timer++ >
                brain_val.limit * ((60 - systemStatus[0] * 10) / systemStatus[1]) / 60) {
                ChangeRunMode(brain_val.runMode == 0);
            }
            _ACTParaStatus_Set((void *)self, brain_val.wait + 5);
            _ACTCharStatus_Set((void *)self, 5, (float)brain_val.wait, 0);
            SetTurnSpeedInEscape(self);
            break;
        case 3:
        case 9:
            brainGirl.minThreshold = 3.0f;
            ACTSendMailCorrect((void *)self, 0x155);
            if (brain_val.timer++ >
                brain_val.limit * ((60 - systemStatus[0] * 10) / systemStatus[1]) / 60) {
                ChangeRunMode(brain_val.runMode == 0);
            }
            _ACTParaStatus_Set((void *)self, brain_val.wait + 8);
            _ACTCharStatus_Set((void *)self, 8, (float)brain_val.wait, 0);
            SetTurnSpeedInEscape(self);
            break;
        case 6: {
            void *look_at = 0;

            brainGirl.minThreshold = 3.0f;
            if (brain_val.modeFrames / ((60 - systemStatus[0] * 10) / systemStatus[1] / 2) & 1) {
                look_at = boyGObj;
            } else if (brain_val.others.num != 0) {
                look_at = brain_val.others.ent[0].obj;
            }
            if (look_at) {
                _ACTLookTarget_Set((void *)self, look_at, 0, 2, 1);
            }
            break;
        }
        }
        brain_val.modeFrames += 1;
        {
            float v = girlBrainGetTargetLevel();
            float c;

            v = _ACTGame_GetParamF(25) + v * (_ACTGame_GetParamF(26) - _ACTGame_GetParamF(25));
            if (v < _ACTGame_GetParamF(25)) {
                c = _ACTGame_GetParamF(25);
            } else if (_ACTGame_GetParamF(26) < v) {
                c = _ACTGame_GetParamF(26);
            } else {
                c = v;
            }
            if (brainGirl.threshold > 1.0f) {
                v = 0.1f;
            } else {
                v = c;
            }
            GOBJ_SUB(self)->root.ikRate0 = v;
        }
        look = 0;
        fire = 0;
        act->flags20.ll &= ~(1LL << 15);
        if (mode == 7 && brain_val.others.num != 0) {
            act->flags20.ll |= 0x8000;
        }
        switch (mode) {
        case 2:
        case 3:
        case 4:
        case 6:
        case 9:
            look = 1;
            act->flags20.ll |= 0x4000;
            break;
        case 8:
            act->flags20.ll |= 0x4000;
            break;
        default:
            act->flags20.ll &= ~0x4000;
            break;
        }
        {
            switch (GirlInfo[1]) {
            case 0:
                GirlInfo[0] = GirlInfo[0] + 1;
                if (look) {
                    GirlInfo[1] = 1;
                    if ((60 - systemStatus[0] * 10) / systemStatus[1] * 30 < GirlInfo[0]) {
                        fire = 1;
                    } else {
                        fire = 0;
                    }
                }
                break;
            case 1:
                if (!look) {
                    GirlInfo[0] = 0;
                    GirlInfo[1] = 0;
                }
                break;
            }
            if (fire && (60 - systemStatus[0] * 10) / systemStatus[1] * 8 / 60 < act->frame) {
                float dir[4];
                int i;

                for (i = 0; i < brain_val.listD.num; i++) {
                    _OrientXZGV(dir, brain_val.listD.ent[i].pos, brain_val.girlPos);
                    if (_AbsRotyGV(dir, test_CURRENTORIENT((void *)self)) < 90) {
                        ACTSendMailCorrect((void *)self, 0xB1);
                        break;
                    }
                }
            }
        }
        if (GOBJ_WORK(self)->mailB1Timer == 0 && ((int *)boyGObj) != 0 &&
            _FrontGV(test_CURRENTROOT((boyGObj)), test_CURRENTROOT((void *)self),
                     test_CURRENTORIENT((void *)self), 90) &&
            ACTGameViewSimple_Check(self, boyGObj)) {
            MotionDef *rec = motionKind + GOBJ_SUB(((int *)boyGObj))->ctrl.motion;
            int send;

            if ((rec->flags.word >> 15) & 1) {
                send = 1;
            } else {
                send = 0;
            }
            if (GOBJ_ACT(((int *)boyGObj))->actMode == 5 && GOBJ_WORK(boyGObj)->modeHist[0] == 41 &&
                GOBJ_ACT(((int *)boyGObj))->modeFrame <
                    (60 - systemStatus[0] * 10) / systemStatus[1] / 3) {
                send = 1;
            }
            if (send) {
                ACTSendMailCorrect((void *)self, 0xB1);
            }
        }
        _ACTWait(1);
    }
}

inline void subGirlBrain_Idle(GObj *volatile self)
{
    char *g = (char *)self;
    GOBJ_ACT(g)->stick.mag = 0;
    _ACTWait(0);
}

inline void subGirlBrain_Becarry(GObj *volatile self)
{
    char *g = (char *)self;
    GOBJ_ACT(g)->stick.mag = 0;
    _ACTWait(0);
}

inline void subGirlBrain_Busy(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    GirlBrainWork *w = &brain_val;
    int i = 0;

    sub->stick.mag = 0;
    while (1) {
        if (w->others.num) {
            _ACTCharStatus_Set((void *)self, 2, -1.0f, (ICO_WORD)w->others.ent[0].obj);
        }
        if (((60 - systemStatus[0] * 10) / systemStatus[1] < i &&
             ACTGameView_Check(self, boyGObj)) ||
            (60 - systemStatus[0] * 10) / systemStatus[1] * 2 < i) {
            w->warn = 1;
        }
        i++;
        _ACTWait(1);
    }
}

/* a file-scope static helper, inlined in subGirlBrain_HideAdvance's move arm
 * and in subGirlBrain_Pulledup */
static inline void girlBrainSetWalkRatio(GObj *g, float ratio) /* derived name */
{
    Act *s = GOBJ_ACT(g);
    float walk = 0.5f;

    if (walk < ratio && ACTWay_IsMustWalkFromWay(g)) {
        s->stick.mag = walk;
    } else {
        s->stick.mag = ratio;
    }
}

void subGirlBrain_Pulledup(GObj *volatile self)
{
    float self_pos[4];
    float boy_pos[4];
    float ofs[4];
    float base[4];
    float top[4];
    float well[4];
    float cur[4];
    float sk[4];
    float oz[4];
    Act *sub = GOBJ_ACT(self);
    int hit;
    float d;
    float dy;
    float lim;
    int ry;

    GetRootProjectionPosOfGObj(self_pos, (void *)self);
    GetRootProjectionPosOfGObj(boy_pos, boyGObj);
    GOBJ_WORK(self)->floorObj = 0;
    sceVu0ScaleVector(ofs, test_CURRENTORIENT((boyGObj)), 60.0f);
    sceVu0AddVector(base, boy_pos, ofs);
    base[1] = base[1] - 50.0f;
    top[0] = base[0];
    top[2] = base[2];
    top[1] = base[1] + 1000.0f;
    if (ACTCheckCollis_WELL(base, top, ((int *)boyGObj), well, 0.0f)) {
        boy_pos[0] = well[0];
        boy_pos[1] = well[1];
        boy_pos[2] = well[2];
        if (floorGObj_ACTCheckCollis_WELL != 0 &&
            ((GObj *)floorGObj_ACTCheckCollis_WELL)->kind == 0x11) {
            GOBJ_WORK(self)->floorObj = floorGObj_ACTCheckCollis_WELL;
        }
        boy_pos[1] = boy_pos[1] - 10.0f;
    }
    if (GOBJ_ACT(((int *)boyGObj))->actMode == 0x58) {
        boy_pos[0] = GOBJ_ACT(((int *)boyGObj))->env.ditchPos[0];
        boy_pos[1] = GOBJ_ACT(((int *)boyGObj))->env.ditchPos[1];
        boy_pos[2] = GOBJ_ACT(((int *)boyGObj))->env.ditchPos[2];
    }
    if (!ACTWayMove_BeginDetail((void *)self, self_pos, boy_pos, 0, 0, 0)) {
        while (1) {
            _ACTWait(1);
        }
    }
    for (;;) {
        cur[0] = test_CURRENTROOT((void *)self)[0];
        cur[1] = test_CURRENTROOT((void *)self)[1];
        cur[2] = test_CURRENTROOT((void *)self)[2];
        hit = ACTWayMove_NextDetail((void *)self, sub->dir, boy_pos, 0, 0);
        debug_NMarker(boy_pos, 0xFF, 0, 0, 100.0f);
        if (!hit) {
            sub->stick.mag = 0;
        } else {
            sub->dir[0] = sub->wayNodeX;
            sub->dir[1] = sub->wayNodeY;
            sub->dir[2] = sub->wayNodeZ;
            girlBrainSetWalkRatio((void *)self, 1.0f);
        }
        d = _DistxzGV(boy_pos, cur);
        dy = boy_pos[1] - cur[1];
        dy = (dy < 0.0f) ? -dy : dy;
        lim = (GOBJ_ACT(((int *)boyGObj))->actMode == 0x58) ? 200.0f : 100.0f;
        if (d < 30.0f && dy < 200.0f) {
            sub->stick.mag = 0;
            GetSkeltonOrient(sk, (void *)self, 0x2C);
            _OrientXZGV(oz, test_CURRENTROOT((boyGObj)), test_CURRENTROOT((void *)self));
            ry = _RotyGV(sk, oz);
            if (0x15 <= ((ry < 0) ? -ry : ry)) {
                if (girlControlMode == 0) {
                    if (0 < ry) {
                        ATGoalTurnSet((void *)self, 3, 2, oz);
                    } else {
                        ATGoalTurnSet((void *)self, 3, 1, oz);
                    }
                }
            } else {
                sub->flags18.ll |= 0x40000000000000;
            }
        } else if (d < lim) {
            sub->stick.mag =
                (sub->stick.mag < 0.0f) ? 0.0f : ((0.5f < sub->stick.mag) ? 0.5f : sub->stick.mag);
        }
        _ACTCharStatus_Set((void *)self, 0x1C, -1.0f, 0);
        GOBJ_WORK(self)->boyDist = _DistGV(test_CURRENTROOT((void *)self), boy_pos);
        _ACTWait(1);
    }
}

#include "girl_brain_attract.c.inc"

static void _girlBrainHide_MakeHidePoint(float *p, float dist)
{
    float v[4];
    ClipWork work;
    int i;
    float total;
    float w;

    if (brain_val.hide.num == 0) {
        return;
    }
    p[0] = 0.0f;
    p[1] = 0.0f;
    p[2] = 0.0f;
    total = p[0];
    for (i = 0; i < brain_val.hide.num; i++) {
        sceVu0SubVector(v, brain_val.boyPos, brain_val.hide.ent[i].pos);
        v[1] = 0.0f;
        sceVu0Normalize(v, v);
        w = brain_val.hide.ent[i].dist;
        if (w < 1.0f) {
            w = 1.0f;
        }
        w = 1.0f / w;
        total += w;
        sceVu0ScaleVector(v, v, w);
        sceVu0AddVector(p, p, v);
    }
    if (total != 0.0f) {
        sceVu0ScaleVector(p, p, dist / total);
    } else {
        debug_assert("src/girl_brain_main.c.inc", 1981);
        __assert("src/girl_brain_main.c.inc", 1981, "0");
    }
    sceVu0Normalize(p, p);
    sceVu0ScaleVector(p, p, dist);
    sceVu0AddVector(p, brain_val.boyPos, p);
    p[1] = brain_val.boyRoot[1];
    work.radius = 50.0f;
    sceVu0CopyVector(work.pt[0], brain_val.girlRoot);
    sceVu0CopyVector(work.pt[1], p);
    ClipWall(&work);
    work.pt[0][0] = work.pt[2][0];
    work.pt[0][2] = work.pt[2][2];
    work.pt[1][0] = work.pt[2][0];
    work.pt[1][2] = work.pt[2][2];
    work.pt[0][1] = work.pt[2][1] - 200.0f;
    work.pt[1][1] = work.pt[2][1] + 200.0f;
    ClipFloor(&work);
    p[0] = work.pt[2][0];
    p[2] = work.pt[2][2];
    p[1] = work.pt[2][1] - 10.0f;
}

static void girlBrainHide_GoalTurn(float *dir, unsigned char sendMail)
{
    float mo[4];
    float eye[4];
    GObj *girl;
    ActWork *p;
    int r;

    girl = girlGObj;
    GetRootMotionOrient(mo, girl);
    r = _RotyGV(mo, dir);
    r = (r < 0) ? -r : r;
    if (r >= 0x2E) {
        p = GOBJ_WORK(girl);
        p->hideDirX = dir[0];
        p->hideDirY = dir[1];
        p->hideDirZ = dir[2];
        GetEyeDirection((char *)eye, girl);
        if (_RotyGV(eye, dir) > 0) {
            debug_StdPrintfDummy("turnL mail\n");
            if (sendMail) {
                ACTSendMailCorrect(girl, 0xEC);
            } else {
                ACTSendMailCorrect(girl, 0xEE);
            }
        } else {
            debug_StdPrintfDummy("turnR mail\n");
            if (sendMail) {
                ACTSendMailCorrect(girl, 0xEB);
            } else {
                ACTSendMailCorrect(girl, 0xED);
            }
        }
    }
}

/* A static inline: hands the girl's sub a move direction at the full run
   ratio.  `run` is declared ahead of the two loads, as this TU's
   girlBrainSetWalkRatio declares its own run/walk ratios. */
static inline void girlBrainSetMoveDir(float *d) /* derived name */
{
    float run = 1.0f;
    Act *s;

    s = GOBJ_ACT(girlGObj);
    s->stick.mag = run;
    s->dir[0] = d[0];
    s->dir[1] = d[1];
    s->dir[2] = d[2];
}

static int isHideRecheck(float *from, float *to, float *root, float *rad)
{
    float v0[4];
    float v1[4];
    float v2[4];
    int i;
    int r;

    sceVu0SubVector(v0, from, root);
    sceVu0SubVector(v1, to, root);
    for (i = 0; i < brain_val.listB.num; i++) {
        sceVu0SubVector(v2, brain_val.listB.ent[i].pos, root);
        r = _RotyGV(v0, v2);
        if ((r < 0 ? -r : r) < 45) {
            *rad = 80.0f;
            return 1;
        }
    }
    r = _RotyGV(v0, v1);
    if (!((r < 0 ? -r : r) < 46)) {
        return 1;
    }
    if (_DistxzSqGV(from, root) < _DistxzSqGV(to, root) && !(_DistxzSqGV(from, to) < 25600.0f)) {
        return 1;
    }
    return 0;
}

void subGirlBrain_Hide(GObj *volatile self)
{
    float hp[4];
    float cand[4];
    int cnt = 0;
    float rad = 80.0f;
    int near;
    /* the girl object handed in by the actor entry, read from the volatile
       parameter once */
    GObj *g;

    /* isHideRecheck, a GNU nested function: it writes the enclosing `rad`
       through the static chain. */

    float dir[4];

    _ACTWait(1);
    hp[0] = brain_val.hidePoint[0];
    hp[1] = brain_val.hidePoint[1];
    hp[2] = brain_val.hidePoint[2];
    while (1) {
        if (cnt++ % ((60 - systemStatus[0] * 10) / systemStatus[1] * 2) == 0 &&
            girlBrainMain_CheckWarningMode(0) != 3) {
            brain_val.warn = 1;
        }
        _girlBrainHide_MakeHidePoint(cand, rad);
        if (isHideRecheck(hp, cand, test_CURRENTROOT((boyGObj)), &rad)) {
            rad = _DistxzGV(test_CURRENTROOT((boyGObj)), test_CURRENTROOT((void *)self));
            rad = (rad < 200.0f) ? 200.0f : ((800.0f < rad) ? 800.0f : rad);
            _girlBrainHide_MakeHidePoint(hp, rad);
            cand[0] = hp[0];
            cand[1] = hp[1];
            cand[2] = hp[2];
        }
        if (isHidePointTooHigh(cand) ||
            girlBrainHideCheckIntercept(brain_val.girlPos, cand, brain_val.hide.ent,
                                        brain_val.hide.num)) {
            brain_val.warn = 1;
        }
        near = _DistxzSqGV(cand, brain_val.girlPos) < 3600.0f;
        if (near) {
            hp[0] = cand[0];
            hp[1] = cand[1];
            hp[2] = cand[2];
        }
        if (_DistxzSqGV(hp, brain_val.girlPos) < 10000.0f || near) {
            g = self;
            GOBJ_ACT(girlGObj)->stick.mag = 0;
            _ACTCharStatus_Set(g, 7, -1.0f, 0);
            if (_DistxzSqGV(hp, brain_val.girlPos) < 6400.0f || near) {
                _OrientXZGV(dir, brain_val.boyPos, brain_val.girlPos);
                girlBrainHide_GoalTurn(dir, 1);
            }
        } else {
            _OrientXZGV(dir, cand, brain_val.girlPos);
            g = self;
            girlBrainSetMoveDir(dir);
            _ACTCharStatus_Set(g, 6, -1.0f, 0);
        }
        _ACTWait(1);
    }
}

inline void subGirlBrain_Hesitate(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    int i = 1;

    for (;;) {
        s->stick.mag = 0.0f;
        if (i % ((60 - systemStatus[0] * 10) / systemStatus[1] / 2) == 0) {
            brain_val.warn = 1;
        }
        i++;
        _ACTWait(1);
    }
}

/* A static inline, inlined three times: true when the straight segment from
   `from` to `to` clears both the wall and the wall-field collision. */
static inline int isNoWallBetween(float *from, float *to) /* derived name */
{
    ClipWork work;

    work.radius = 10.0f;
    sceVu0CopyVector(work.pt[0], from);
    sceVu0CopyVector(work.pt[1], to);
    ClipWall(&work);
    if (work.wall.elem == 0) {
        ClipWallField(&work);
        if (work.wall.elem == 0) {
            return 1;
        }
    }
    return 0;
}

/* A static inline: true when `b` is within 100 units vertically and 100
   units in the plane of `a`. */
static inline unsigned char isNearPoint(float *a, float *b) /* derived name */
{
    if ((a[1] - b[1] < 0.0f ? -(a[1] - b[1]) : a[1] - b[1]) < 100.0f) {
        if (_DistSqGV(a, b) < 10000.0f) {
            return 1;
        }
    }
    return 0;
}

/* A static inline, inlined twice inside girlBrainRunawaySearchPoint: true
   when no listB entry lies closer to `p` than the girl does, and no listB
   entry lies closer to `p` than it lies to the girl. */
static inline unsigned char isRunawayPointClear(float *p, float *girl) /* derived name */
{
    int i;
    float d;

    d = _DistSqGV(p, girl);
    for (i = 0; i < brain_val.listB.num; i++) {
        if (_DistSqGV(p, brain_val.listB.ent[i].pos) < d) {
            return 0;
        }
    }
    for (i = 0; i < brain_val.listB.num; i++) {
        float dg = _DistSqGV(girl, brain_val.listB.ent[i].pos);
        float dp = _DistSqGV(p, brain_val.listB.ent[i].pos);

        if (dp < dg) {
            return 0;
        }
    }
    return 1;
}

static int CorrectList(float (*list)[4], float *p, int n)
{
    int i;
    int num;

    num = 0;
    for (i = 0; i < n; i++) {
        if (!isNearPoint(p, list[i])) {
            float *d = correctListWork[num];

            d[0] = list[i][0];
            d[1] = list[i][1];
            d[2] = list[i][2];
            num++;
        }
    }
    for (i = 0; i < num; i++) {
        float *s = correctListWork[i];

        list[i][0] = s[0];
        list[i][1] = s[1];
        list[i][2] = s[2];
    }
    return num;
}

static int girlBrainRunawaySearchPoint(float *goal, float *out, float *p)
{
    /* CorrectList, a GNU nested function placed inside its parent's body.  It
       reads nothing of the parent's frame (its scratch list is a file
       static). */

    GObj *g;
    Act *sub;
    int n;
    int i;
    int correct = 1;

    g = girlGObj;
    sub = GOBJ_ACT(g);
    n = GetNearNigePointN(runawayPointList, 10, &sub->way, p);
    for (i = n - 1; i >= 0; i--) {
        float (*list)[4] = runawayPointList;

        if (isNearPoint(p, list[i])) {
            p[0] = list[i][0];
            p[1] = list[i][1];
            p[2] = list[i][2];
            n = GetNearNigePointN(list, 10, &sub->way, p);
            break;
        }
    }
    /* a local switch holding a constant guards the list correction */
    if (correct) {
        n = CorrectList(runawayPointList, p, n);
    }
    for (i = 0; i < n; i++) {
        _DistGV(p, runawayPointList[i]);
        _DistGV(brain_val.listB.ent[0].pos, runawayPointList[i]);
        if (isNearPoint(p, runawayPointList[i])) {}
    }
    for (i = 0; i < n; i++) {
        if (isRunawayPointClear(runawayPointList[i], p)) {
            dispWayMarker(runawayPointList[i]);
        }
    }
    for (i = 0; i < n; i++) {
        if (isRunawayPointClear(runawayPointList[i], p)) {
            float pos[4];
            float *s;

            GetWay_begin(runawayPointList[i], &sub->way, goal);
            sub->way.reached = 0;
            pos[0] = goal[0];
            pos[1] = goal[1];
            pos[2] = goal[2];
            pos[1] -= 50.0f;
            if (isNoWallBetween(pos, runawayPointList[i])) {
                sub->way.reached = 1;
                DeleteGuideWay(&sub->way);
            }
            s = runawayPointList[i];
            out[0] = s[0];
            out[1] = s[1];
            out[2] = s[2];
            brain_val.lastWay = 0;
            return 1;
        }
    }
    return 0;
}

static int girlBrainRunawayMoveByWay(GObj *self, float *out, float *tgt)
{
    float d[4];
    float pos[4];
    Act *sub;
    WayPoint *way;
    int done;

    sub = GOBJ_ACT(self);
    d[0] = tgt[0];
    d[1] = tgt[1];
    d[2] = tgt[2];
    switch (sub->way.reached) {
    case 0:
        done = 0;
        GetRootProjectionPosOfGObj(pos, self);
        pos[1] = pos[1] - 0.0f;
        way = GetWay_next(&sub->way, pos);
        if (way == 0) {
            sceVu0CopyVector(out, sub->way.nrm);
        } else {
            sceVu0CopyVector(out, sub->way.nrm);
            if (brain_val.lastWay != way) {
                if (brain_val.lastWay != 0) {
                    done = sub->way.guideFirst < 1;
                }
                brain_val.lastWay = way;
            }
        }
        if (sub->way.chk.cross == 0) {
            if (isNoWallBetween(pos, d)) {
                sub->way.reached = 1;
                DeleteGuideWay(&sub->way);
            }
        }
        if (done) {
            return 2;
        }
        break;
    case 1: {
        float cur[4];
        float dir[4];

        GetRootPosition(cur, self);
        if (isNoWallBetween(cur, d)) {
            float dx = d[0];
            float dz = d[2];

            dir[0] = dx - cur[0];
            dir[1] = 0.0f;
            dir[2] = dz - cur[2];
            sceVu0Normalize(out, dir);
            if (isNearPoint(d, cur)) {
                return 1;
            }
        }
    } break;
    }
    return 0;
}

/* A static inline: points `v` at the first listB entry within 300 units of
   `p` whose bearing from `dir` is under 45 degrees. */
static inline void girlBrainEscapeFaceCheck(float *p, float *dir, Vec4u *v) /* derived name */
{
    int i;

    memset(v, 0, 16);
    v->f[3] = 1.0f;
    for (i = 0; i < brain_val.listB.num; i++) {
        if (_DistSqGV(p, brain_val.listB.ent[i].pos) < 90000.0f) {
            sceVu0SubVector(v->f, brain_val.listB.ent[i].pos, p);
            if ((_RotGV(dir, v->f) < 0 ? -_RotGV(dir, v->f) : _RotGV(dir, v->f)) < 45) {
                break;
            }
        }
    }
}

/* girlBrainDebugPrint in subGirlBrain_Escape: called in arm 1 after
   `mode = 2` and after `mode = 3`, and in cases 1 and 2 after
   girlBrainRunawayMoveByWay. */

/* The move-state names subGirlBrain_Escape prints, defined here before it. */
static char *moveStateName[5] = {"IDLE", "MOVE START", "MOVE LOOP", "END",
                                 "WAIT"}; /* derived name */

void subGirlBrain_Escape(GObj *volatile self)
{
    float pos[4];
    float ppos[4];
    float rp[4];
    Vec4u v;
    float mk[4];
    float mtx[16];
    Act *sub;
    int cnt;
    int mode;
    int i;
    int ret;

    cnt = 1;
    sub = GOBJ_ACT(self);
    mode = 0;
    for (i = 0; i < (60 - systemStatus[0] * 10) / systemStatus[1] / 2; i++) {
        sub->stick.mag = 0;
        _ACTWait(1);
    }
    for (;;) {
        if (sub->actMode == 0x6F) {
            mode = 4;
        }
        if (brain_val.listB.num == 0 ||
            cnt++ % ((60 - systemStatus[0] * 10) / systemStatus[1] / 2) == 0) {
            brain_val.warn = 1;
        }
        GetRootPosition(pos, (void *)self);
        GetRootProjectionPosOfGObj(ppos, (void *)self);
        switch (mode) {
        case 0:
            mode = 1;
            GetRootProjectionPosOfGObj(rp, (void *)self);
            brain_val.runawayFrom[0] = rp[0];
            brain_val.runawayFrom[1] = rp[1];
            brain_val.runawayFrom[2] = rp[2];
            sub->stick.mag = 0;
            break;
        case 1:
            mode = 2;
            girlBrainDebugPrint();
            GetRootProjectionPosOfGObj(rp, (void *)self);
            if (girlBrainRunawaySearchPoint(rp, brain_val.runawayGoal, brain_val.runawayFrom) ==
                0) {
                mode = 3;
                girlBrainDebugPrint();
            }
            sub->stick.mag = 0;
            break;
        case 2:
            ret = girlBrainRunawayMoveByWay(self, (float *)&sub->dir[0], brain_val.runawayGoal);
            switch (ret) {
            case 0:
                break;
            case 1:
                girlBrainDebugPrint();
                brain_val.runawayFrom[0] = brain_val.runawayGoal[0];
                brain_val.runawayFrom[1] = brain_val.runawayGoal[1];
                brain_val.runawayFrom[2] = brain_val.runawayGoal[2];
                mode = 1;
                break;
            case 2:
                girlBrainDebugPrint();
                brain_val.runawayFrom[0] = ppos[0];
                brain_val.runawayFrom[1] = ppos[1];
                brain_val.runawayFrom[2] = ppos[2];
                mode = 1;
                break;
            }
            girlBrainEscapeFaceCheck(pos, (float *)&sub->dir[0], &v);
            switch (brain_val.runMode) {
            case 0:
                girlBrainSetWalkRatio((void *)self, 1.0f);
                break;
            case 1:
                sub->stick.mag = 0.5f;
                break;
            }
            if (sub->stick.mag != 0.0f) {
                sceVu0ScaleVector(v.f, (float *)&sub->dir[0], 300.0f);
                sceVu0AddVector(v.f, brain_val.girlPos, v.f);
                if (girlBrainHideCheckIntercept(brain_val.girlPos, v.f, brain_val.hide.ent,
                                                brain_val.hide.num)) {
                    sub->stick.mag = 0.0f;
                    brain_val.warn = 1;
                }
            }
            sceVu0ScaleVector(mk, brain_val.runawayGoal, -1.0f);
            debug_Marker(mk, 0xFF, 0, 0, 200.0f, (float)brain_val.markerPulse);
            brain_val.markerPulse = brain_val.markerPulse + 5;
            break;
        case 3:
            sub->stick.mag = 0;
            ACTSendMailCorrect((void *)self, 0xE5);
            brainGirl.minThreshold = 9.0f;
            if ((((int)(((ActStatus *)&sub->flags18.ll)->ll >> 63)) & 1) ||
                (sub->flags20.i[0] & 1)) {
                sub->flags20.ll = sub->flags20.ll | 4;
            }
            break;
        case 4:
            sub->stick.mag = 0;
            if (sub->actMode != 0x6F) {
                mode = 0;
            }
            break;
        }
        MatrixDrive_PushMatrix();
        sceVu0TransposeMatrix(mtx, ((float *)((float *)matrixptr)) + 32);
        mtx[3] = mtx[7] = mtx[11] = 0.0f;
        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        MatrixDrive_TransMatrix(pos[0], pos[1], pos[2]);
        sceVu0MulMatrix(MatrixDrive_GetMatrix(), MatrixDrive_GetMatrix(), mtx);
        MatrixDrive_PushMatrix();
        MatrixDrive_TransMatrix(0.0f, -50.0f, 0.0f);
        sprintf(escapeDebugString, "%s", moveStateName[mode]);
        DispWireString(escapeDebugString);
        MatrixDrive_PopMatrix();
        MatrixDrive_PopMatrix();
        _ACTWait(1);
    }
}

void ClipTwinVector(float *out, float *from, float *to, float max)
{
    float d;
    float t;

    d = _DistGV(from, to);
    if (max < d) {
        t = (d - max) / d;
        _InterGV(out, from, to, t, 1.0f - t);
    } else {
        out[0] = to[0];
        out[1] = to[1];
        out[2] = to[2];
    }
}

/* `inline`: the four Danger_* GetSafePosition bodies below expand it and its
   out-of-line copy closes the TU's deferred run. */
inline int ACTCheckCollis_SAFE(float height, float *p0, float *p1, void *actor, float *posout,
                               int radius)
{
    ClipWork work;
    float tmp[4];
    int flag;
    int rv;

    rv = 1;
    flag = actor ? GOBJ_SUB(actor)->disp : 0;

    work.radius = (float)radius;
    work.pt[0][0] = p0[0];
    work.pt[0][1] = p0[1];
    work.pt[0][2] = p0[2];
    work.pt[1][0] = p1[0];
    work.pt[1][2] = p1[2];
    work.pt[1][1] = p0[1];

    tmp[0] = p1[0];
    tmp[1] = p0[1];
    tmp[2] = p1[2];

    if (flag != 0) {
        GOBJ_SUB(actor)->disp = 0;
    }
    ClipWall(&work);
    if (work.wall.elem == 0) {
        ClipWallField(&work);
        if (work.wall.elem == 0)
            goto no_wall;
    }
    tmp[0] = work.pt[2][0];
    tmp[1] = work.pt[2][1];
    tmp[2] = work.pt[2][2];
no_wall:
    work.pt[0][0] = tmp[0];
    work.pt[0][1] = tmp[1];
    work.pt[0][2] = tmp[2];
    work.pt[1][0] = tmp[0];
    work.pt[1][2] = tmp[2];
    work.pt[1][1] = tmp[1] + height;
    ClipFloor(&work);
    if (work.floor.elem == 0) {
        rv = 0;
    } else {
        work.pt[2][1] -= 10.0f;
    }
    if (posout != 0) {
        posout[0] = work.pt[2][0];
        posout[1] = work.pt[2][1];
        posout[2] = work.pt[2][2];
    }
    if (flag != 0) {
        GOBJ_SUB(actor)->disp = 1;
    }
    return rv;
}

static void Danger_Bomb(GObj *self);
static void Danger_Gondola(GObj *self);
static void Danger_Box(GObj *self);
static void Danger_Rotobject(GObj *self);

inline void subGirlBrain_DangerEnv(GObj *volatile self)
{
    switch (dangerEnv.kind) {
    case 1:
        Danger_Bomb((void *)self);
        break;
    case 2:
        Danger_Gondola((void *)self);
        break;
    case 3:
        Danger_Box((void *)self);
        break;
    case 4:
        Danger_Rotobject((void *)self);
        break;
    }
    _ACTWait(0);
}

static int Danger_Bomb_GetSafePosition(float rad, float *dst, float *center, float *cur,
                                       ICO_WORD ok)
{
    float dir[4];
    float pos[4];
    float best;
    float d;
    int found;
    int i;

    best = 0.0f;
    found = 0;
    for (i = 0; i < 8; i++) {
        memset(dir, 0, 0x10);
        dir[2] = rad;
        _ApplyRyGV(dir, (float)(i * 45 - 180) * 3.1415927f / 180.0f);
        sceVu0AddVector(pos, center, dir);
        ok = ACTCheckCollis_SAFE(200.0f, center, pos, 0, pos, 40);
        if (ok) {
            d = _DistGV(center, pos);
            if (best < d) {
                best = d;
                dst[0] = pos[0];
                dst[1] = pos[1];
                dst[2] = pos[2];
                found = 1;
            }
        }
    }
    if (found && _DistSqGV(dst, center) < _DistSqGV(cur, center)) {
        dst[0] = cur[0];
        dst[1] = cur[1];
        dst[2] = cur[2];
    }
    return found;
}

static void Danger_Bomb(GObj *self)
{
    /* GetSafePosition, a GNU nested function; each Danger_* parent carries
     * its own copy.  The float radius is the first parameter, and the last
     * parameter, the boy position the callers hand in and this copy never
     * reads, is reused as the collision flag. */
    float goal[4];
    float girl[4];
    float obj[4];
    float base[4];
    float tmp[4];
    float now[4];
    float dir[4];
    Act *sub;
    void *bomb;
    long long f;
    unsigned char r;
    unsigned char r2;
    long long p;
    int turn;

    sub = GOBJ_ACT(self);
    bomb = dangerEnv.obj;
retry:
    {
        GetRootProjectionPosOfGObj(goal, bomb);
        GetRootProjectionPosOfGObj(girl, self);
        obj[0] = test_CURRENTROOT(bomb)[0];
        obj[1] = test_CURRENTROOT(bomb)[1];
        obj[2] = test_CURRENTROOT(bomb)[2];
        GetRootProjectionPosOfGObj(base, bomb);
        _ACTWait(1);
        Danger_Bomb_GetSafePosition(500.0f, goal, test_CURRENTROOT(bomb), girl,
                                    (ICO_WORD)test_CURRENTROOT((boyGObj)));
        _ACTWait(1);
        p = ACTWayMove_BeginDetail(self, girl, goal, 0, 0, 0);
        r = p;
        if (!r) {
            _ACTWait(0);
        }
        _ACTWait(1);
        turn = 0;
        while (1) {
            _ACTCharStatus_Set(self, 11, -1.0f, (ICO_WORD)bomb);
            GetRootProjectionPosOfGObj(tmp, bomb);
            GetRootProjectionPosOfGObj(girl, self);
            GetRootProjectionPosOfGObj(now, bomb);
            if (!(_DistxzSqGV(now, base) < 10000.0f)) {
                goto retry;
            }
            p = ACTWayMove_NextDetail(self, sub->dir, goal, 0, 0);
            r2 = p;
            f = sub->wayFlags;
            if (((int)(f >> 16) & 1)) {
                turn = 0;
            }
            if (!r2) {
                turn = 1;
            } else if (((int)(f >> 17) & 1)) {
                turn = 1;
            } else if (sub->wayGoalDist < 50.0f) {
                turn = 1;
            } else if (turn == 0) {
                sub->dir[0] = sub->wayNodeX;
                sub->dir[1] = sub->wayNodeY;
                sub->dir[2] = sub->wayNodeZ;
                sub->stick.mag = 1.0f;
            }
            if (turn) {
                sub->stick.mag = 0.0f;
                _OrientXZGV(dir, obj, test_CURRENTROOT(self));
                girlBrainHide_GoalTurn(dir, 1);
            }
            _ACTWait(1);
        }
    }
}

static int Danger_Gondola_GetSafePosition(float rad, float *dst, float *center, float *cur,
                                          ICO_WORD ok)
{
    float dir[4];
    float pos[4];
    float base[4];
    float best;
    float d;
    int found;
    int i;

    best = 0.0f;
    found = 0;
    for (i = 0; i < 8; i++) {
        memset(dir, 0, 0x10);
        dir[2] = rad;
        _ApplyRyGV(dir, (float)(i * 45 - 180) * 3.1415927f / 180.0f);
        sceVu0AddVector(pos, center, dir);
        pos[1] = cur[1];
        base[0] = cur[0];
        base[2] = cur[2];
        base[1] = cur[1] - 70.0f;
        ok = ACTCheckCollis_SAFE(200.0f, base, pos, 0, pos, 40);
        if (ok) {
            d = _DistxzSqGV(center, pos);
            if (best < d) {
                best = d;
                dst[0] = pos[0];
                dst[1] = pos[1];
                dst[2] = pos[2];
                found = 1;
            }
        }
    }
    if (found && _DistSqGV(dst, center) < _DistSqGV(cur, center)) {
        dst[0] = cur[0];
        dst[1] = cur[1];
        dst[2] = cur[2];
    }
    return found;
}

static void Danger_Gondola(GObj *self)
{
    /* Danger_Gondola's own copy of the nested GetSafePosition: the candidate
     * is tested from 70 below the current position at the current height,
     * and ranked by the XZ distance from the centre. */
    float goal[4];
    float girl[4];
    float obj[4];
    float base[4];
    float tmp[4];
    float now[4];
    float dir[4];
    void *gondola;
    Act *sub;
    long long f;
    unsigned char r;
    long long p;
    int turn;

    sub = GOBJ_ACT(self);
    gondola = dangerEnv.obj;
retry:
    {
        GetRootProjectionPosOfGObj(goal, gondola);
        GetRootProjectionPosOfGObj(girl, self);
        obj[0] = test_CURRENTROOT(gondola)[0];
        obj[1] = test_CURRENTROOT(gondola)[1];
        obj[2] = test_CURRENTROOT(gondola)[2];
        GetRootProjectionPosOfGObj(base, gondola);
        _ACTWait(1);
        Danger_Gondola_GetSafePosition(300.0f, goal, test_CURRENTROOT(gondola), girl,
                                       (ICO_WORD)test_CURRENTROOT((boyGObj)));
        _ACTWait(1);
        p = ACTWayMove_BeginDetail(self, girl, goal, 0, 0, 0);
        r = p;
        if (!r) {
            _ACTWait(0);
        }
        _ACTWait(1);
        turn = 0;
        while (1) {
            _ACTCharStatus_Set(self, 11, -1.0f, (ICO_WORD)gondola);
            GetRootProjectionPosOfGObj(tmp, gondola);
            GetRootProjectionPosOfGObj(girl, self);
            GetRootProjectionPosOfGObj(now, gondola);
            if (!(_DistxzSqGV(now, base) < 10000.0f)) {
                goto retry;
            }
            p = ACTWayMove_NextDetail(self, sub->dir, goal, 0, 0);
            r = p;
            debug_NMarker(goal, 0xFF, 0, 0, 100.0f);
            f = sub->wayFlags;
            if (((int)(f >> 16) & 1)) {
                turn = 0;
            }
            if (!r) {
                turn = 1;
            } else if (((int)(f >> 17) & 1)) {
                turn = 1;
            } else if (sub->wayGoalDist < 50.0f) {
                turn = 1;
            } else if (turn == 0) {
                sub->dir[0] = sub->wayNodeX;
                sub->dir[1] = sub->wayNodeY;
                sub->dir[2] = sub->wayNodeZ;
                sub->stick.mag = 1.0f;
            }
            if (debug_font_flag & 1) {
                debug_Printf(10, 110, 0x0FFFFFFF, "goal[%d]\n", turn);
            }
            if (turn) {
                sub->stick.mag = 0.0f;
                _OrientXZGV(dir, obj, test_CURRENTROOT(self));
                girlBrainHide_GoalTurn(dir, 0);
            }
            _ACTWait(1);
        }
    }
}

/* the three escape angles Danger_Box's safe-position search tries */
static int dangerEscapeAngle[3] = {0, -90, 90}; /* derived name */

static int Danger_Box_GetSafePosition(float *dst, float *way, float *center, float rad, ICO_WORD ok,
                                      int mode, float *girl)
{
    float dir[4];
    float pos[4];
    float from[4];
    float best;
    float d;
    int found;
    int i;
    int ang;

    best = 0.0f;
    found = 0;
    for (i = 0; i < 3; i++) {
        memset(dir, 0, 0x10);
        dir[2] = rad;
        if (i == 0 && !isBoyPushBoxTruck()) {
            continue;
        }
        ang = (int)(_GetDirection(test_CURRENTORIENT((boyGObj))) / 3.1415927f * 180.0f) +
              dangerEscapeAngle[i];
        if (ang >= 181) {
            ang -= 360;
        }
        if (ang <= -181) {
            ang += 360;
        }
        _ApplyRyGV(dir, (float)ang * 3.1415927f / 180.0f);
        sceVu0AddVector(pos, center, dir);
        pos[1] = center[1];
        from[0] = center[0];
        from[2] = center[2];
        from[1] = center[1] - 70.0f;
        ok = ACTCheckCollis_SAFE(200.0f, from, pos, 0, pos, 40);
        if (ok) {
            d = _DistxzSqGV(way, pos);
            if (mode != 1) {
                if (_DistxzSqGV(pos, way) < _DistxzSqGV(way, center)) {
                    continue;
                }
            }
            if (_DistxzSqGV(pos, girl) < 3600.0f) {
                continue;
            }
            if (best < d) {
                best = d;
                dst[0] = pos[0];
                dst[1] = pos[1];
                dst[2] = pos[2];
                found = 1;
            }
        }
    }
    if (found) {
        /* both calls are evaluated and the comparison dropped: the body of
         * this test is empty in the shipped build */
        if (_DistSqGV(dst, way) < _DistSqGV(center, way)) {}
    } else {
        dst[0] = center[0];
        dst[1] = center[1];
        dst[2] = center[2];
    }
    return found;
}

static void Danger_Box(GObj *self)
{
    /* GetSafePosition, a GNU nested function; see Danger_Bomb for the
     * parameter-order note.  This copy takes six integer parameters; the
     * fifth (the boy root the callers hand in) is never read and is reused
     * as the collision flag. */
    float goal[4];
    float girl[4];
    float objp[4];
    float way[4];
    float cur[4];
    float boxp[4];
    float sideA[4];
    float sideB[4];
    float ofs[4];
    float orient[4];
    float tmp[4];
    float dir[4];
    Act *sub;
    void *box;
    float rad;
    long long f;
    unsigned char r;
    unsigned char r2;
    long long p;
    int turn;

    sub = GOBJ_ACT(self);
    rad = isBoyPushBoxTruck() ? 400.0f : 200.0f;
    box = dangerEnv.obj;
    GetRootProjectionPosOfGObj(goal, box);
    GetRootProjectionPosOfGObj(girl, self);
    objp[0] = test_CURRENTROOT(box)[0];
    objp[1] = test_CURRENTROOT(box)[1];
    objp[2] = test_CURRENTROOT(box)[2];
    GetRootProjectionPosOfGObj(way, box);
    orient[0] = test_CURRENTORIENT((boyGObj))[0];
    orient[1] = test_CURRENTORIENT((boyGObj))[1];
    orient[2] = test_CURRENTORIENT((boyGObj))[2];
    boxp[0] = (test_CURRENTROOT(GOBJ_ACT(((int *)boyGObj))->box))[0];
    boxp[1] = (test_CURRENTROOT(GOBJ_ACT(((int *)boyGObj))->box))[1];
    boxp[2] = (test_CURRENTROOT(GOBJ_ACT(((int *)boyGObj))->box))[2];
    cur[0] = test_CURRENTROOT(self)[0];
    cur[1] = test_CURRENTROOT(self)[1];
    cur[2] = test_CURRENTROOT(self)[2];
    sceVu0ScaleVector(ofs, orient, 100.0f);
    sceVu0AddVector(sideA, boxp, ofs);
    sceVu0ScaleVector(ofs, orient, -100.0f);
    sceVu0AddVector(sideB, boxp, ofs);
    if (_DistSqGV(sideA, cur) < _DistSqGV(sideB, cur)) {
        way[0] = sideA[0];
        way[1] = sideA[1];
        way[2] = sideA[2];
    } else {
        way[0] = sideB[0];
        way[1] = sideB[1];
        way[2] = sideB[2];
    }
    _ACTWait(1);
    if (!Danger_Box_GetSafePosition(goal, way, girl, rad, (ICO_WORD)test_CURRENTROOT((boyGObj)), 0,
                                    girl)) {
        GetRootProjectionPosOfGObj(cur, box);
        if (!Danger_Box_GetSafePosition(goal, way, cur, rad, (ICO_WORD)test_CURRENTROOT((boyGObj)),
                                        1, girl)) {
            debug_StdPrintfDummy("box escape position not found");
            goal[0] = girl[0];
            goal[1] = girl[1];
            goal[2] = girl[2];
        }
    }
    _ACTWait(1);
    p = ACTWayMove_BeginDetail(self, girl, goal, 0, 0, 0);
    r = p;
    if (!r) {
        _ACTWait(0);
    }
    sub->wayState.flags |= 0x10000;
    _ACTWait(1);
    turn = 0;
    while (1) {
        _ACTCharStatus_Set(self, 11, -1.0f, (ICO_WORD)box);
        GetRootProjectionPosOfGObj(cur, box);
        GetRootProjectionPosOfGObj(girl, self);
        GetRootProjectionPosOfGObj(tmp, box);
        p = ACTWayMove_NextDetail(self, sub->dir, goal, 0, 0);
        r2 = p;
        f = sub->wayFlags;
        if (((int)(f >> 16) & 1)) {
            turn = 0;
        }
        if (!r2) {
            turn = 1;
        } else if (((int)(f >> 17) & 1)) {
            turn = 1;
        } else if (sub->wayGoalDist < 50.0f) {
            turn = 1;
        } else if (turn == 0) {
            sub->dir[0] = sub->wayNodeX;
            sub->dir[1] = sub->wayNodeY;
            sub->dir[2] = sub->wayNodeZ;
            sub->stick.mag = 1.0f;
        }
        if (turn) {
            sub->stick.mag = 0.0f;
            _OrientXZGV(dir, objp, test_CURRENTROOT(self));
            girlBrainHide_GoalTurn(dir, 0);
        }
        _ACTWait(1);
    }
}

static int Danger_Rotobject_GetSafePosition(float rad, float *dst, float *center, float *girl,
                                            ICO_WORD ok)
{
    float boy[4];
    float dir[4];
    float pos[4];
    float from[4];
    float best;
    float d;
    int found;
    int i;

    boy[0] = test_CURRENTROOT((boyGObj))[0];
    boy[1] = test_CURRENTROOT((boyGObj))[1];
    boy[2] = test_CURRENTROOT((boyGObj))[2];
    best = 0.0f;
    found = 0;
    for (i = 0; i < 4; i++) {
        memset(dir, 0, 0x10);
        dir[2] = rad;
        _ApplyRyGV(dir, (float)(i * 90 - 135) * 3.1415927f / 180.0f);
        sceVu0AddVector(pos, center, dir);
        pos[1] = girl[1];
        from[0] = girl[0];
        from[1] = girl[1] - 70.0f;
        from[2] = girl[2];
        ok = ACTCheckCollis_SAFE(200.0f, from, pos, 0, pos, 30);
        if (ok) {
            d = _DistxzSqGV(boy, pos);
            if (best < d) {
                best = d;
                dst[0] = pos[0];
                dst[1] = pos[1];
                dst[2] = pos[2];
                found = 1;
            }
        }
    }
    return found;
}

static void Danger_Rotobject(GObj *self)
{
    /* GetSafePosition, a GNU nested function; see Danger_Bomb for the
     * parameter-order note. */
    float goal[4];
    float girl[4];
    float objp[4];
    float base[4];
    float tmp1[4];
    float tmp2[4];
    float dir[4];
    Act *sub;
    void *obj;
    int turn;

    sub = GOBJ_ACT(self);
    obj = dangerEnv.obj;
    GetRootProjectionPosOfGObj(goal, obj);
    GetRootProjectionPosOfGObj(girl, self);
    objp[0] = test_CURRENTROOT(obj)[0];
    objp[1] = test_CURRENTROOT(obj)[1];
    objp[2] = test_CURRENTROOT(obj)[2];
    GetRootProjectionPosOfGObj(base, obj);
    _ACTWait(1);
    if (!Danger_Rotobject_GetSafePosition(300.0f, goal, test_CURRENTROOT(obj), girl,
                                          (ICO_WORD)test_CURRENTROOT((boyGObj)))) {
        while (1) {
            sub->stick.mag = 0.0f;
            _ACTWait(1);
        }
    }
    turn = 0;
    _ACTWait(1);
    while (1) {
        _ACTCharStatus_Set(self, 11, -1.0f, (ICO_WORD)obj);
        GetRootProjectionPosOfGObj(tmp1, obj);
        GetRootProjectionPosOfGObj(girl, self);
        GetRootProjectionPosOfGObj(tmp2, obj);
        if (_DistxzSqGV(girl, goal) < 3600.0f) {
            turn = 1;
        } else {
            _OrientXZGV(sub->dir, goal, girl);
            sub->stick.mag = 1.0f;
        }
        if (turn) {
            sub->stick.mag = 0.0f;
            _OrientXZGV(dir, objp, test_CURRENTROOT(self));
            girlBrainHide_GoalTurn(dir, 0);
        }
        _ACTWait(1);
    }
}

void subGirlBrain_HideAdvance(GObj *volatile self)
{
    float self_pos[4];
    float boy_pos[4];
    Act *sub = GOBJ_ACT(self);
    long long p;
    unsigned char hit;

    GetRootProjectionPosOfGObj(self_pos, (void *)self);
    GetRootProjectionPosOfGObj(boy_pos, boyGObj);
    ACTWayMove_BeginDetail((void *)self, self_pos, boy_pos, 0, 0, 0);
    for (;;) {
        brain_val.hideAdvWait = (60 - systemStatus[0] * 10) / systemStatus[1];
        GetRootProjectionPosOfGObj(boy_pos, boyGObj);
        p = ACTWayMove_NextDetail((void *)self, sub->dir, boy_pos, 0, 0);
        hit = p;
        debug_NMarker(boy_pos, 0xFF, 0, 0, 100.0f);
        if (!hit ||
            (sub->wayGoalDist < 100.0f &&
             (sub->wayGoalHeight < 0.0f ? -sub->wayGoalHeight : sub->wayGoalHeight) < 100.0f)) {
            sub->stick.mag = 0;
            _ACTWait(1);
            continue;
        }
        {
            /* the actor-entry home is `volatile` (the scheduler rewrites the GObj
             * slot between waits), so the arm reads it once at its top and
             * works from the captured pointer */
            void *g = (void *)self;

            sub->dir[0] = sub->wayNodeX;
            sub->dir[1] = sub->wayNodeY;
            sub->dir[2] = sub->wayNodeZ;
            girlBrainSetWalkRatio(g, 1.0f);
        }
        _ACTWait(1);
    }
}

static int isEnterHideadv_EnemyLocation(float *bpos, float *gpos)
{
    float o1[4];
    float o2[4];
    int i;
    float d;

    bpos[0] = test_CURRENTROOT((boyGObj))[0];
    bpos[1] = test_CURRENTROOT((boyGObj))[1];
    bpos[2] = test_CURRENTROOT((boyGObj))[2];
    gpos[0] = test_CURRENTROOT((void *)girlGObj)[0];
    gpos[1] = test_CURRENTROOT((void *)girlGObj)[1];
    gpos[2] = test_CURRENTROOT((void *)girlGObj)[2];
    _OrientXZGV(o1, bpos, gpos);
    for (i = 0; i < brain_val.hide.num; i++) {
        if (((GObj *)brain_val.hide.ent[i].obj)->kind == 4) {
            d = _DistGV(gpos, brain_val.hide.ent[i].pos);
            if (!(1500.0f < d)) {
                if (_DistSqGV(bpos, brain_val.hide.ent[i].pos) < (d + 100.0f) * (d + 100.0f)) {
                    return 0;
                }
                if (d < 150.0f) {
                    return 0;
                }
                _OrientXZGV(o2, brain_val.hide.ent[i].pos, gpos);
                if (_AbsRotyGV(o1, o2) < 45) {
                    return 0;
                }
            }
        }
    }
    return 1;
}

static int isEnterHideadv(void)
{
    float buf[8];
    int rv = 0;
    float diff;
    if (((int *)boyGObj) == 0) {
        goto ret0;
    }
    if ((void *)girlGObj == 0) {
        return 0;
    }
    GetRootProjectionPosOfGObj(buf, boyGObj);
    GetRootProjectionPosOfGObj(buf + 4, girlGObj);
    diff = buf[1] - buf[5];
    if (diff < 0.0f) {
        if (-diff > 200.0f) {
            goto set;
        }
        goto test;
    }
    if (diff > 200.0f) {
    set:
        rv = 1;
    }
test:
    if (rv == 0) {
        goto ret0;
    }
    if (_DistxzSqGV(buf, buf + 4) < 22500.0f) {
        return 1;
    }
    if (_DistxzSqGV(buf, buf + 4) < 250000.0f) {
        if (isEnterHideadv_EnemyLocation(buf, buf + 4) != 0) {
            return 1;
        }
    }
ret0:
    return 0;
}

inline void *FindGirlPullupFloorBoxGObj(void)
{
    void *g = girlGObj;
    if (brain_val.curMode == 7 && GOBJ_ACT(((int *)boyGObj))->actMode == 0x4E) {
        return GOBJ_WORK(g)->floorObj;
    }
    return 0;
}

static unsigned char wayTestBegin = 1; /* derived name */

static unsigned char wayTestMoving = 0; /* derived name */

void WayTest(void)
{
    /* .sbss, girl_act.o's first word: how many frames in a row WayTest has
       seen the girl's heading swing by more than 90 units */
    static int wayTurnFrames;
    float a[4];
    float b[4];
    Act *s;
    void *g;
    int r;

    g = girlGObj;
    s = GOBJ_ACT(g);
    GetRootProjectionPosOfGObj(b, g);
    GetRootProjectionPosOfGObj(a, boyGObj);
    if ((pad[0].flags & 8) || wayTestBegin) {
        debug_StdPrintfDummy("begin");
        wayTestMoving = ACTWayMove_BeginDetail(g, b, a, ((int *)boyGObj), 0, 0);
        wayTestBegin = 0;
    }
    if (wayTestMoving) {
        if (!ACTWayMove_NextDetail(g, s->dir, a, 0, 0)) {
            debug_StdPrintfDummy("next error");
        }
        dispWayMarker((float *)&s->wayFromX);
    }
    if (s->wayGoalDist < 100.0f) {
        s->stick.mag = 0.0f;
    } else {
        s->stick.mag = 1.0f;
    }
    r = _RotyGV(&s->wayNodeX, s->dir);
    r = (r < 0) ? -r : r;
    if (r >= 0x5B) {
        wayTurnFrames = wayTurnFrames + 1;
        s->dir[0] = s->wayNodeX;
        s->dir[1] = s->wayNodeY;
        s->dir[2] = s->wayNodeZ;
    } else {
        s->dir[0] = s->wayNodeX;
        s->dir[1] = s->wayNodeY;
        s->dir[2] = s->wayNodeZ;
        wayTurnFrames = 0;
    }
    dispWayMarker(s->way.chk.cur->pos);
    dispWayMarker(s->way.chk.start->pos);
}

/* The attract-state names, defined here after WayTest. */
static char *wayTestStateName[9] = {
    "IDLE",    "SEARCHWAY", "LOSTTWAY", "APPROACH", "GOAL",
    "ATTRACT", "LOOKONLY",  "ATTRLOOK", "FINISH"}; /* derived name */

/* girl_act.o's .data global after the attract-state names: the brain's work
   record, all zero, explicitly initialised so that it stays in .data. */
GirlBrainWork brain_val = {0};

/* girl_act.o's .sdata globals (declared in girl_act.h) */
int hyde_test = 0;

int padtimer_stand = 0;

int padtimer_walk = 0;

int padtimer_run = 0;

/* the motion-def row of an actor's current motion (boyact.c's CHAINROW) */
#define MOTDIRROW(self) (GOBJ_SUB(self)->ctrl.motion + motionKind) /* derived name */

/* The girl's control thread.  A block of compiled-out debug code sits in
   it; the strings it printed ("src/girl_act.c", "NOTARGET",
   "[%s] %4d %4d %4d", "delete wg 2\n") stay in .rodata between this
   function's "girl no!!\n" and its jump table. */
void subGirlControl(GObj *volatile self)
{
    float dir[4];
    /* unused here: a vector of the compiled-out block */
    float vec[4];

    /* the target and the actor pointer as one frame record; the target is
       an object handle, a word like the object's actor slot it is stored
       beside */
    struct { /* field names derived */
        int target;
        Act *p;
    } w;

    float mdir[4];
    /* unused here: the locals of the compiled-out block */
    float work[124];
    int hold;
    int push;
    int lim;

    w.p = GOBJ_ACT(self);
    w.target = 0;
    while (w.p->motReq == 0) {
        debug_StdPrintfDummy("girl no!!\n");
        _ACTWait(1);
    }
    iosPadConnect(&w.p->pad, 0, 1, &w.p->padConf);
    girlPad = &w.p->pad;
    for (;;) {
        if ((int)(w.p->flags18.ll >> 48) & 1) {
            if (scpBoyControlReadDisable == 0 &&
                ((void *)self == (void *)CurrentTargetGObj || girlControlMode != 0))
                /* the pad read, once; the listing puts its exit branch on the while line */
                do {
                    iosPadConnect(&w.p->pad, 0, girlControlMode != 0, &w.p->padConf);
                    iosPadRead(&w.p->pad);
                    iosPadGetStick(&w.p->pad, &w.p->stick, 0, 2, 2, debug_stick_simulate);
                    _GetMotionDirection(mdir, (void *)self);
                    w.p->stick.angle = CorrectStickInfo(mdir, &w.p->stick);
                    if (w.p->stick.mag > 0.001f) {
                        ConvertStickToAbsCoord(dir, &w.p->stick);
                        w.p->dir[0] = dir[0];
                        w.p->dir[1] = dir[1];
                        w.p->dir[2] = dir[2];
                    }
                } while (0);
            else if ((void *)self == (void *)CurrentTargetGObjSub) {
                iosPadConnect(&w.p->pad, 0, 1, &w.p->padConf);
            } else {
                iosPadConnect(&w.p->pad, 0, 1, &w.p->padConf);
            }
            /* the compiled-out block */
            if (0) {
                debug_assert("src/girl_act.c", 1654);
                __assert("src/girl_act.c", 1654, "0");
                debug_StdPrintfDummy("NOTARGET");
                debug_StdPrintfDummy("[%s] %4d %4d %4d");
                debug_StdPrintfDummy("delete wg 2\n");
                debug_StdPrintfDummy("SLOW");
                debug_StdPrintfDummy("HURRY");
                debug_StdPrintfDummy("STOP");
                debug_StdPrintfDummy("target");
            }
            if (girlControlMode == 0 && ((w.p->flags18.ll & 0x3000000000000000) != 0 ||
                                         ((int)(w.p->flags20.ll >> 7) & 1))) {
                if (w.p->stick.mag != 0.0f) {
                    w.p->stick.mag = 0.5f;
                }
            }
        }
        if (!(w.p->stick.mag > 0.1f)) {
            padtimer_stand = padtimer_stand + 1;
        } else {
            padtimer_stand = 0;
        }
        if (w.p->stick.mag > 0.1f && (w.p->stick.mag < 0.99f || (w.p->pad.now & 0x20))) {
            padtimer_walk = padtimer_walk + 1;
        } else {
            padtimer_walk = 0;
        }
        if (0.1f < w.p->stick.mag &&
            !(0.1f < w.p->stick.mag && (w.p->stick.mag < 0.99f || (w.p->pad.now & 0x20)))) {
            padtimer_run = padtimer_run + 1;
        } else {
            padtimer_run = 0;
        }
        dir[0] = w.p->dir[0];
        dir[1] = w.p->dir[1];
        dir[2] = w.p->dir[2];
        switch ((unsigned int)w.p->actMode) {
        case 0x45:
        case 0x50:
        case 0x73:
            break;
        default:
            if (w.p->stick.mag > 0.1f && w.p->actMode != 0x73) {
                SetMotionDirectionSmooze(
                    self, dir,
                    (float)(((void *)self == (void *)girlGObj && girlControlMode != 0)
                                ? MOTDIRROW(self)->girlDirFrames
                                : MOTDIRROW(self)->dirFrames));
            }
            break;
        }
        _ACTCommonMailTest((void *)self, padtimer_stand, padtimer_walk, padtimer_run);
        switch (w.p->actMode) {
        case 1:
            ACTSendMailCorrect(self, 0xC7);
            break;
        case 2:
            ACTSendMailCorrect(self, 0xB5);
            break;
        case 3:
            ACTSendMailCorrect(self, 0xBA);
            break;
        case 29:
            if (!(girlControlMode != 0 && ((int *)GOBJ_WORK(self)->modeHist)[0] == 4 &&
                  ((int)(w.p->flags18.ll >> 56) & 1))) {
                if ((w.p->stick.mag > 0.1f &&
                     (w.p->stick.angle < -134 || 134 < w.p->stick.angle)) ||
                    (w.p->pad.trg & 0x40)) {
                    if (100.0f < GetDifferenceFromLowerField((void *)self, 0x2C)) {
                        ACTSendMailCorrect(self, 0x127);
                    } else {
                        ACTSendMailCorrect(self, 0xE2);
                    }
                }
            }
            if ((w.p->stick.mag > 0.1f && (w.p->stick.angle >= -45 && w.p->stick.angle <= 45)) ||
                (w.p->pad.trg & 0x10)) {
                ACTSendMailCorrect(self, 0xC7);
            }
            if (w.p->stick.mag > 0.1f && (w.p->stick.angle >= 46 && w.p->stick.angle <= 134)) {
                ACTSendMailCorrect(self, 0x14E);
            }
            if (w.p->stick.mag > 0.1f) {
                if (-134 <= w.p->stick.angle) {
                    if (w.p->stick.angle < -45) {
                        ACTSendMailCorrect(self, 0x14F);
                    }
                }
            }
            break;
        case 28:
            if (!(girlControlMode != 0 && ((int *)GOBJ_WORK(self)->modeHist)[0] == 4 &&
                  ((int)(w.p->flags18.ll >> 56) & 1)) &&
                ((w.p->stick.mag > 0.1f && (w.p->stick.angle < -134 || 134 < w.p->stick.angle)) ||
                 (w.p->pad.trg & 0x40))) {
                ACTSendMailCorrect(self, 0xE2);
            } else {
                if (girlControlMode != 0 ||
                    (60 - systemStatus[0] * 10) / systemStatus[1] * 5 < w.p->modeFrame) {
                    if (w.p->stick.mag > 0.1f &&
                        (w.p->stick.angle >= -45 && w.p->stick.angle <= 45)) {
                        ACTSendMailCorrect(self, 0xC7);
                    }
                }
            }
            ACTSendMailCorrect(self, 0x150);
            break;
        case 15:
            if (w.p->pad.trg & 0x20) {
                ACTSendMailCorrect(self, 0xC7);
            }
            break;
        case 38:
            if (((int *)boyGObj) != 0) {
                if (GOBJ_WORK(boyGObj)->footIkFrames >
                    (60 - systemStatus[0] * 10) / systemStatus[1] / 3) {
                    ACTSendMailCorrect(self, 0x14A);
                }
                if (GOBJ_WORK(boyGObj)->bit37Frames >
                    (60 - systemStatus[0] * 10) / systemStatus[1] / 3) {
                    ACTSendMailCorrect(self, 0x14B);
                }
            }
            lim = w.p->stick.y - 128;
            if (100 < lim) {
                ACTSendMailCorrect(self, 0x14B);
            } else if (lim < -100) {
                ACTSendMailCorrect(self, 0x14A);
            } else {
                ACTSendMailCorrect(self, 0x150);
            }
            break;
        case 45:
            if ((girlControlMode != 0 ? (w.p->stick.mag > 0.1f &&
                                         (w.p->stick.angle >= -45 && w.p->stick.angle <= 45))
                                      : (w.p->stick.mag > 0.1f)) ||
                ((int)(w.p->flags20.ll >> 12) & 1)) {
                ACTSendMailCorrect(self, 0x75);
                ACTSendMailCorrect(self, 0x74);
            }
            hold = 0;
            push = 0;
            if (((int *)boyGObj) != 0 && girlControlMode == 0) {
                if (GOBJ_ACT(((int *)boyGObj))->actMode == 0x2D) {
                    push = GOBJ_ACT(((int *)boyGObj))->intrMot != 0x45;
                } else {
                    hold = 1;
                }
            }
            if (hold) {
                (GOBJ_ACT(self)->enemy->dirSmoothFrames)++;
            } else {
                GOBJ_ACT(self)->enemy->dirSmoothFrames = 0;
            }
            if (push) {
                (GOBJ_ACT(self)->enemy->dirSmoothFrames2)++;
            } else {
                GOBJ_ACT(self)->enemy->dirSmoothFrames2 = 0;
            }
            if (GOBJ_ACT(self)->enemy->dirSmoothFrames >
                (60 - systemStatus[0] * 10) / systemStatus[1] / 2) {
                ACTSendMailCorrect(self, 0x75);
                ACTSendMailCorrect(self, 0x74);
            }
            if (GOBJ_ACT(self)->enemy->dirSmoothFrames2 >
                (60 - systemStatus[0] * 10) / systemStatus[1] / 2) {
                ACTSendMailCorrect(self, 0x74);
            }
            break;
        }
        *(unsigned long long *)&w.p->flags20 &= ~0x4000000ULL;
        if (girlControlMode != 0 && (w.p->pad.now & 8)) {
            *(unsigned long long *)&w.p->flags20 |= 0x4000000ULL;
        }
        _ACTWait(1);
    }
}

extern int fptodp(float v);

void subGirlCollision(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    float ori[4];
    float dir[4];
    float eye[4];
    float sk[4];
    float oz[4];
    int cnt = 0;
    int cnt2 = 0;
    float ang;
    float rot;
    float dirAng;
    float sideAng;
    int hold;
    int step;
    int flag;
    int ry;
    int rz;
    MotionDef *rec;
    const ActModeRec *attr;
    GObj *p;

    while (sub->motReq == 0) {
        _ACTWait(1);
    }
    if (GetDifferenceFromLowerField((void *)self, 0x2C) > 200.0f) {
        ACTSendMailCorrect((void *)self, 7);
    }
    for (;;) {
        flag = 0;
        ACTGame_CommonLoop((void *)self);
        brainLevelProcess(&brainGirl);
        ACTLookTargetSystem_Exec((void *)self);
        ACTParaStatus_Exec((void *)self);
        if (sub->stick.mag != 0.0f) {
            switch ((unsigned int)sub->actMode) {
            case 1:
                ang = _ACTGame_GetParamF(35);
                if (ang < 60.0f) {
                    ang = 60.0f;
                }
                break;
            case 2:
                ang = _ACTGame_GetParamF(36);
                if (((int *)GOBJ_WORK(self)->modeHist)[0] == 10 &&
                    sub->modeFrame < (60 - systemStatus[0] * 10) / systemStatus[1] / 3) {
                    if (ang < 60.0f) {
                        ang = 60.0f;
                    }
                }
                break;
            case 3:
                ang = _ACTGame_GetParamF(37);
                if (((int *)GOBJ_WORK(self)->modeHist)[0] == 10 &&
                    sub->modeFrame < (60 - systemStatus[0] * 10) / systemStatus[1] / 3) {
                    if (ang < 60.0f) {
                        ang = 60.0f;
                    }
                }
                break;
            default:
                ang = 105.0f;
                break;
            }
            dir[0] = sub->dir[0];
            dir[1] = sub->dir[1];
            dir[2] = sub->dir[2];
            GetRootMotionOrient(ori, self);
            rot = (float)_RotyGV(ori, dir);
            dir[1] = ori[1] = 0.0f;
            sceVu0Normalize(dir, dir);
            sceVu0Normalize(ori, ori);
            if (ang < ((rot < 0.0f) ? -rot : rot)) {
                sub->env.turnDir.f[0] = dir[0];
                sub->env.turnDir.f[1] = dir[1];
                sub->env.turnDir.f[2] = dir[2];
                GetEyeDirection((char *)eye, self);
                rot = (float)_RotyGV(eye, dir);
                rec = motionKind + GOBJ_SUB(self)->ctrl.motion;
                if (rec->flags.word & 1) {
                    padtimer_run = 0;
                    padtimer_walk = 0;
                }
                if (rot > 0.0f) {
                    ACTSendMailCorrect((void *)self, 0xE8);
                } else {
                    ACTSendMailCorrect((void *)self, 0xE7);
                }
            } else {
                GetSkeltonOrient(sk, (void *)self, 1);
                if (debug_font_flag & 1) {
                    debug_Printf(10, 100, 0x0FFFFFFF, "[%d]\n", _AbsRotyGV((char *)sub->dir, sk));
                }
                if (_AbsRotyGV((char *)sub->dir, sk) < 60) {
                    ACTSendMailCorrect((void *)self, 0xF0);
                }
                debug_Arrow(100.0f, test_CURRENTROOT((void *)self), sk, 0, 0xFF, 0);
            }
        } else if (sub->flags20.ll & 6) {
            if (!ACTGame_FLAG_TETSUNAGI()) {
                dirAng = ((int)(sub->flags20.ll >> 1) & 1) ? 90.0f : 60.0f;
                sideAng = ((int)(sub->flags20.ll >> 1) & 1) ? 150.0f : 130.0f;
                step = ((int)(sub->flags20.ll >> 1) & 1) ? 60 : 2;
                hold = ((int)(sub->flags20.ll >> 2) & 1) && cnt2 > 0;
                GetSkeltonOrient(sk, (void *)self, 35);
                GetSkeltonOrient(eye, (void *)self, 44);
                _OrientXZGV(oz, GOBJ_SUB(self)->root.lookPos, test_CURRENTROOT((void *)self));
                ry = _AbsRotyGV(eye, sk);
                rz = _AbsRotyGV(eye, oz);
                if (dirAng < ry && sideAng < rz) {
                    flag = 1;
                }
                if (step * ((60 - systemStatus[0] * 10) / systemStatus[1]) / 60 < cnt && !hold) {
                    if ((int)(sub->flags18.ll >> 63) & 1) {
                        ATGoalTurnSet((void *)self, 1, 1, oz);
                    } else {
                        ATGoalTurnSet((void *)self, 1, 2, oz);
                    }
                }
            }
        }
        cnt = flag ? cnt + 1 : 0;
        if (sub->actMode == 10) {
            cnt2 = (60 - systemStatus[0] * 10) / systemStatus[1] * 4;
        }
        if (cnt2) {
            cnt2--;
        }
        ACTGame_SaveActorInformation(self);
        if (((int *)boyGObj) != 0 && (void *)girlGObj != 0) {
            if (!ACTGame_CheckHandMotion(boyGObj, girlGObj)) {
                ACTSendMailCorrect((void *)self, 0x3E);
                debug_StdPrintfDummy("mot error\n");
            }
            if (!(_DistSqGV(test_CURRENTROOT((boyGObj)), test_CURRENTROOT((void *)girlGObj)) <
                  250000.0f)) {
                if (GOBJ_SUB(((int *)boyGObj))->root.hand1.mode == 5) {
                    debug_StdPrintfDummy("hand connect error\n");
                }
                ACTGame_DisconnectHand();
                debug_StdPrintfDummy("dist fatal error, [%f]\n",
                                     fptodp(_DistGV(test_CURRENTROOT((boyGObj)),
                                                    test_CURRENTROOT((void *)girlGObj))));
                ACTSendMailCorrect((void *)self, 0x3E);
            }
        }
        switch ((unsigned int)sub->actMode) {
        case 0x2C: {
            sceVu0FVECTOR home = {814.0f, 1827.0f, 865.0f, 1.0f};
            sceVu0FVECTOR gate = {608.0f, 1600.0f, -775.0f, 1.0f};

            if (!gflagChk(106)) {
                _OrientXZGV(sub->dir, gate, test_CURRENTROOT((void *)self));
            } else if (test_CURRENTROOT((void *)self)[2] > 350.0f) {
                _OrientXZGV(sub->dir, home, test_CURRENTROOT((void *)self));
            } else {
                sub->dir[0] = 0.0f;
                sub->dir[1] = 0.0f;
                sub->dir[2] = -1.0f;
            }
            SetMotionDirection((void *)self, (float *)sub->dir);
            sub->stick.mag = 1.0f;
            ACTSendMailCorrect((void *)self, 0xDD);
            break;
        }
        case 1:
            ACTSendMailCorrect((void *)self, 0xC7);
            break;
        case 2:
        case 3:
            break;
        }
        p = GOBJ_SUB(self)->parent.obj;
        if (p != 0 && p->kind == 17) {
            if (GetBoxMode(p) == 2) {
                ACTSendMailCorrect((void *)self, 0x105);
            }
        }
        ATGoalTurnSendMail((void *)self);
        attr = &actModeTbl[sub->actMode];
        if (attr->softIk) {
            GOBJ_SUB(self)->root.ikRate0 = 0.3f;
        } else {
            GOBJ_SUB(self)->root.ikRate0 = GOBJ_WORK(self)->defIkRate0;
        }
        if (girlControlMode != 0 && stage_no == 23 && GOBJ_SUB(self)->ctrl.upperWall != 0 &&
            GOBJ_SUB(self)->ctrl.upperWallDist < 50.0f) {
            if (sub->actMode == 36) {
                ACTSendMailCorrect((void *)self, 0x1AC);
            }
            if (sub->actMode == 1 && (((int *)GOBJ_WORK(self)->modeHist)[0] == 28 ||
                                      ((int *)GOBJ_WORK(self)->modeHist)[0] == 36)) {
                ACTSendMailCorrect((void *)self, 0x1AC);
            }
        }
        _ACTWait(1);
    }
}

/* this TU's uses of GetHeightOfFieldPlaneDifference do not fit the
   prototype in motionManager2.h */

inline int NotNeedBackHand(void)
{
    GObj *g = girlGObj;
    Act *w = GOBJ_ACT(g);

    if ((((int)(w->flags18.ll >> 40)) & 1) == 0) {
        return 1;
    }
    if (w->actMode == 0x45 && handmgr.near90 != 0 && handmgr.still == 0) {
        return 1;
    }
    return 0;
}

inline void afterGirlHand(ICO_WORD_PTR(GObj *) self)
{
    ICO_WORD_PTR(GObj *) volatile local = self;
    ACTGame_DisconnectHand();
    debug_StdPrintfDummy("after func\n");
    iosPadActStop(7);
    ACTWay_SetBeginPositionIllegal((GObj *)local);
}

/* the hand manager's record, all zero and explicitly initialised so that it
   stays in .data */
GirlStand handmgr = {0};

#include "girl_act_hand.c.inc"

static void GetBoyMode(int *mode, int *p1, int *p2, int *p3)
{
    MotionDef *rec;
    *mode = GOBJ_ACT(boyGObj)->actMode;
    *p1 = 0;
    *p2 = 0;
    *p3 = 0;
    switch (*mode) {
    case 14:
        *mode = 1;
        break;
    case 15:
        *mode = 1;
        break;
    case 8:
        *mode = 1;
        break;
    case 2:
    case 3:
        if (GOBJ_ACT(boyGObj)->curItem != 0) {
            *mode = 2;
        }
        rec = motionKind + GOBJ_SUB(boyGObj)->ctrl.motion;
        switch ((rec->modeBits.word >> 22) & 3) {
        case 1:
            *mode = 2;
            break;
        case 2:
            *mode = 3;
            break;
        }
        if (*mode == 3) {
            unsigned long long f = ICO_RAW(unsigned long long, GOBJ_WORK(boyGObj), 0x448,
                                           ((ActStatusWord *)&GOBJ_WORK(boyGObj)->stopFrames)->q);
            if ((int)(f >> 33) & 1) {
                *mode = 1;
            } else if ((int)(f >> 32) & 1) {
                *mode = 2;
            }
        }
        if (*mode == 2) {
            unsigned long long f = ICO_RAW(unsigned long long, GOBJ_WORK(boyGObj), 0x448,
                                           ((ActStatusWord *)&GOBJ_WORK(boyGObj)->stopFrames)->q);
            if ((int)(f >> 33) & 1) {
                *mode = 1;
            }
        }
        break;
    case 36:
        if (GOBJ_ACT(boyGObj)->intrMot == 0x5E) {
            *p2 = 1;
        } else {
            *mode = 1;
        }
        break;
    case 5:
    case 13:
    case 17:
    case 18:
    case 68:
        *mode = 3;
        break;
    }
}

void actGirlHand(GObj *volatile self)
{
    float look[4];
    float d[4];
    float dir[4];
    float gpos[4];
    float bpos[4];
    float v60[4];
    float v70[4];
    float v80[4];
    float v90[4];
    float vA0[4];
    void *target;
    int mode;
    int p1;
    int p2;
    int p3;
    int cnt;
    int cnt2;
    Act *sub;
    MotionDef *rec;
    unsigned char *box;
    int n;
    int n1;
    int n2;
    int v;
    int st;
    int r;
    int flag;
    int grab;
    int over;
    float hand;
    float dist;
    float t;
    float turn;
    float h;
    float rate;

    sub = GOBJ_ACT(self);
    cnt = 0;
    v = 0;
    n = 0;
    st = 0;
    cnt2 = 0;
    if (brain_val.lastTarget == ((int *)boyGObj)) {
        target = brain_val.lastTarget;
        GirlBrainClearTarget((void *)self, &target);
    }
    sub->after = (void *)afterGirlHand;
    ACTGame_ConnectHand();
    HandMgr_Init();
    for (;;) {
        sub->flags20.ll &= ~0x100000;
        GetBoyMode(&mode, &p1, &p2, &p3);
        sceVu0ScaleVector(look, test_CURRENTORIENT((void *)self), 200.0f);
        look[1] = 0.0f;
        sceVu0AddVector(look, test_CURRENTROOT((void *)self), look);
        _ACTLookTarget_Set((void *)self, 0, look, 1, 1);
        if (ACTGame_FLAG_TETSUNAGI() == 0) {
            ACTSendMailCorrect((void *)self, 0x3E);
            debug_StdPrintfDummy("?\n");
        }
        if (GOBJ_SUB(self)->root.hand0.mode == 6) {
            HandMgr_Update();
            HandMgr_Judge();
            n++;
            if (handmgr.far135 == 0) {
                n = 0;
            }
            grab = 0;
            if (handmgr.far125 || handmgr.far135) {
                grab = 1;
            }
            over = 0;
            if (n >= 3) {
                over = 1;
            }
            if (p2 != 0) {
                over = 0;
            }
            if (over) {
                ACTSendMailCorrect((void *)self, 0x3E);
                debug_StdPrintfDummy("dist error\n");
            } else if (grab) {
                h = GetHeightOfFieldPlaneDifference(boyGObj, girlGObj) < 0.0f
                        ? -GetHeightOfFieldPlaneDifference(boyGObj, girlGObj)
                        : GetHeightOfFieldPlaneDifference(boyGObj, girlGObj);
                if (h < 150.0f) {
                    _OrientXZGV(d, test_CURRENTROOT((boyGObj)), test_CURRENTROOT((void *)self));
                    if (((int)(sub->flags18.ll >> 44) & 1) &&
                        _AbsRotyGV(d, (char *)sub->env.wallOrient) >= 0x88) {
                        iosOmSendMail(boyGObj, 0xF7, isysCurrentGObj);
                    } else {
                        iosOmSendMail(boyGObj, 0xF8, isysCurrentGObj);
                    }
                }
            }
        }
        if (GOBJ_SUB(self)->root.hand0.mode == 6) {
            static float pullLen = 0.0f;  /* derived name */
            static float pullTurn = 0.0f; /* derived name */

            hand = (float)(((void *)self == (void *)girlGObj && girlControlMode != 0)
                               ? MOTDIRROW(self)->girlDirFrames
                               : MOTDIRROW(self)->dirFrames);
            t = 200.0f;
            n1 = GetSkeltonFocusNode((void *)girlGObj, 0x12);
            CopyVector(gpos, (char *)GOBJ_SUB(girlGObj)->nodeMtx + n1 * 0x40 + 0x30);
            n2 = GetSkeltonFocusNode(boyGObj, 2);
            CopyVector(bpos, (char *)GOBJ_SUB(((int *)boyGObj))->nodeMtx + n2 * 0x40 + 0x30);
            sceVu0SubVector(d, test_CURRENTROOT((boyGObj)), test_CURRENTROOT((void *)girlGObj));
            sceVu0SubVector(d, bpos, gpos);
            dist = FSqrt(sceVu0InnerProduct(d, d));
            v60[0] = bpos[0];
            v60[1] = bpos[1];
            v60[2] = bpos[2];
            v70[0] = gpos[0];
            v70[1] = gpos[1];
            v70[2] = gpos[2];
            sceVu0SubVector(v80, v60, v70);
            sceVu0Normalize(v80, v80);
            r = _RotyGV(test_CURRENTORIENT((boyGObj)), v80);
            if (r < 0) {
                turn = 0.0f;
            } else if ((r < 0 ? -r : r) < 10) {
                turn = 0.0f;
            } else if ((r < 0 ? -r : r) >= 0x88) {
                turn = 0.0f;
            } else {
                turn = r / 30.0f;
            }
            if (turn < 0.0f) {
                rate = 0.0f;
            } else if (1.0f < turn) {
                rate = 1.0f;
            } else {
                rate = turn;
            }
            t = t * rate;
            pullLen = pullLen + (t - pullLen) * 0.1f;
            sceVu0ScaleVector(vA0, v80, pullLen);
            _ApplyRyGV(vA0, -1.5707964f);
            sceVu0AddVector(v90, v60, vA0);
            {
                float near;

                sceVu0SubVector(dir, v90, v70);
                sceVu0Normalize(dir, dir);
                near = dist / 90.0f;
                /* h, free since the height test, holds the clamped factor */
                h = near < 1.0f ? 1.0f
                                : (3.40282347e+38f /* FLT_MAX */ < near ? 3.40282347e+38f : near);
                hand = hand * h;
            }
            sceVu0ScaleVector(v60, dir, 300.0f);
            sceVu0AddVector(v60, test_CURRENTROOT((void *)self), v60);
            if (handmgr.turned != 0) {
                if (handmgr.far100 != 0) {
                    float goal = 30.0f;

                    pullTurn = pullTurn + (goal - pullTurn) *
                                              (((int)(sub->flags18.ll >> 44) & 1) ? 0.7f : 0.1f);
                } else {
                    pullTurn = hand;
                }
            } else {
                pullTurn = hand;
            }
            SetMotionDirectionSmooze(self, dir, pullTurn);
            sub->dir[0] = dir[0];
            sub->dir[1] = dir[1];
            sub->dir[2] = dir[2];
            switch (mode) {
            case 2:
                cnt++;
                break;
            case 1:
            case 3:
                cnt = 0;
                break;
            case 0x37:
                st = 0;
                break;
            }
            if (p3) {
                st = 1;
                cnt++;
            }
            if (GOBJ_ACT(((int *)boyGObj))->actMode == 3) {
                if (GOBJ_ACT(((int *)boyGObj))->intrMot == 0xA1) {
                    st = 0;
                }
                if ((int)(GOBJ_ACT(((int *)boyGObj))->flags18.ll >> 45) & 1) {
                    st = 0;
                }
            }
            if ((int)(sub->flags20.ll >> 19) & 1) {
                ACTSendMailCorrect((void *)self, 0x3E);
                st = 0;
            }
            if (st == 0) {
                v = 0;
            }
            if (st == 1) {
                v = 0;
            }
            if (st == 2) {
                v++;
            }
            if (mode == 1) {
                if ((int)(GOBJ_ACT(girlGObj)->flags20.ll >> 36) & 1) {
                    st = 0;
                }
            }
            if (GOBJ_WORK(self)->orientFrames > (60 - systemStatus[0] * 10) / systemStatus[1]) {
                st = 0;
            }
            switch (st) {
            case 0:
                hand = 90.0f;
                if (mode == 2) {
                    hand = 60.0f;
                }
                sub->motReq = SetMotionRequest((void *)self, 1, sub->env.motOriReq);
                ACTGame_SetMotionPlaySpeedRatio_Reserve((void *)self, 1.0f, 2);
                if (hand < dist) {
                    rec = motionKind + GOBJ_SUB(self)->ctrl.motion;
                    if (((rec->flags.word >> 29) & 1) == 0 || mode != 1) {
                        st = 1;
                        if (mode == 3) {
                            st = 2;
                        }
                    }
                }
                break;
            case 1: {
                float speed = (dist - 80.0f) / 80.0f + 0.6f;
                sub->motReq = SetMotionRequest((void *)self, 0xE, sub->env.motOriReq);
                HandMgr_Speed(self, speed);
                if (mode == st || p1 != 0) {
                    if (dist < 80.0f) {
                        st = 0;
                    }
                }
                ICO_RAW(long long, sub, 0x20, sub->flags20.ll) |= 8;
                if (mode == 3) {
                    st = 2;
                }
                break;
            }
            case 2: {
                float speed = (dist - 90.0f) * 4.0f / 90.0f + 1.0f;
                sub->motReq = SetMotionRequest((void *)self, 0x10, sub->env.motOriReq);
                ICO_RAW(long long, sub, 0x20, sub->flags20.ll) |= 0x100000;
                HandMgr_Speed(self, speed);
                if (mode == 1) {
                    st = 3;
                    if (v >= 0x33) {
                        st = 0;
                    }
                }
                if (mode == 2) {
                    if (cnt >= 0xB) {
                        st = 1;
                    }
                }
                sub->flags20.ll |= 8;
                break;
            }
            case 3:
                sub->motReq = SetMotionRequest((void *)self, 0xE, sub->env.motOriReq);
                st = 0;
                break;
            }
        }
        flag =
            handmgr.far100 && (box = (unsigned char *)&GOBJ_WORK(girlGObj)->handCl)[1] && box[0x20];
        if (flag) {
            cnt2++;
        } else {
            cnt2 = cnt2 / 2;
        }
        if ((60 - systemStatus[0] * 10) / systemStatus[1] / 5 < cnt2) {
            iosOmSendMail((void *)girlGObj, 0x3E, boyGObj);
        }
        ACTSendMailCorrect((void *)self, 0x1AB);
        _ACTWait(1);
    }
}

void actGirlPulledReady(GObj *volatile self)
{
    float boy[4];
    float dir[4];
    float pos[4];
    float dst[4];
    float d;
    float v;

    GetRootPosition(pos, (void *)self);
    PAIR_GetPosition_BOY(boy, dir);
    sceVu0ScaleVector(dst, dir, 30.0f);
    sceVu0AddVector(dst, boy, dst);
    dst[1] = test_CURRENTROOT((void *)self)[1];
    sceVu0ScaleVector(dir, dir, -1.0f);
    d = _DistxzGV(pos, dst) * 0.5f;
    v = (d < 1.0f) ? 1.0f : ((d > 20.0f) ? 20.0f : d);
    StartCorrectPosition((void *)self, dst, dir, 1, v);
    while (IsCorrectPosition((void *)self)) {
        ContinueCorrectPosition((void *)self);
        _ACTWait(1);
    }
    while (1) {
        if (!PAIR_IsStatus_BOY_PULL()) {
            ACTSendMailCorrect((void *)self, 0x50);
        }
        _ACTWait(1);
    }
}

inline void afterGirlPulledGo(void *self)
{
    void *volatile q = self;
    int *p = (int *)GOBJ_SUB(q);
    ((struct Sub15C *)p)->ctrl.keepWall = 0;
}

void actGirlPulledGo(GObj *volatile self)
{
    float q[4];
    Act *s;
    int m;

    s = GOBJ_ACT(self);
    s->after = (void *)afterGirlHand;
    ACTGame_ConnectHand();
    *(MotOriReq *)&GOBJ_SUB(self)->root.wall = s->env.motOriReq;
    ICO_RAW(int, *(char **)((char *)self + 0x15C), 0x634, GOBJ_SUB(self)->ctrl.keepWall) = 1;
#ifdef ICO_HOST
    ACT_AFTER_PROC(s) = (void (*)(GObj *))afterGirlPulledGo;
#else
    *(char **)((char *)s + 0x18) = (char *)afterGirlPulledGo;
#endif
    memset(q, 0, 0x10);
    q[3] = 1.0f;
    RotQuaternionY(q, 0);
    SetMotionNodeFixModeParameter(girlGObj, boyGObj, 2, 6, q, 0.0f, 0.0f, 0.0f, 1.0f);
    while (1) {
        if (!PAIR_IsStatus_BOY_PULL()) {
            ACTSendMailCorrect((void *)self, 0x53);
            ACTSendMailCorrect((void *)self, 0x54);
        } else {
            m = GOBJ_ACT(((int *)boyGObj))->actMode;
            if (m == 2 || m == 3) {
                ACTSendMailCorrect((void *)self, 0x55);
                ACTSendMailCorrect((void *)self, 0x56);
            }
            if ((60 - systemStatus[0] * 10) / systemStatus[1] * 2 < s->modeFrame) {
                ACTSendMailCorrect((void *)self, 0x57);
            }
        }
        _ACTWait(1);
    }
}

void actGirlDitch3mReady(GObj *volatile self)
{
    float gpos[4];
    float now[4];
    float bpos[4];

    GetRootPosition(now, (void *)self);
    PAIR_GetPosition_BOY_DITCH(bpos, gpos);
    bpos[1] = test_CURRENTROOT((void *)self)[1];
    StartCorrectPosition((void *)self, bpos, gpos, 1, 20.0f);
    while (IsCorrectPosition((void *)self)) {
        debug_NMarker(bpos, 0, 0, 0xFF, 100.0f);
        ContinueCorrectPosition((void *)self);
        _ACTWait(1);
    }
    while (1) {
        if (!PAIR_IsStatus_BOY_DITCH()) {
            ACTSendMailCorrect((void *)self, 0x18E);
        }
        _ACTWait(1);
    }
}

inline void actGirlDitch3mExec(GObj *volatile self)
{
    float boy[4];
    float girl[4];
    float dst[4];
    int i = 0;
    int go = 1;

    ACTGame_ConnectHand();
    debug_StdPrintfDummy("ditch3m hand connect\n");
    /* disabled in retail: the way-begin report of the ditch jump */
    if (0) {
        debug_StdPrintfDummy("WBP set [ditch jump]\n");
    }
    for (;;) {
        if (go) {
            boy[0] = test_CURRENTROOT((boyGObj))[0];
            boy[1] = test_CURRENTROOT((boyGObj))[1];
            boy[2] = test_CURRENTROOT((boyGObj))[2];
            girl[0] = test_CURRENTROOT((void *)girlGObj)[0];
            girl[1] = test_CURRENTROOT((void *)girlGObj)[1];
            girl[2] = test_CURRENTROOT((void *)girlGObj)[2];
            _InterGV(dst, boy, girl, 1.0f, 1.0f);
            SetRootPosition((void *)self, dst);
            i++;
            go = i < 3;
        }
        ACTSendMailCorrect((void *)self, 0x193);
        _ACTWait(1);
    }
}

inline void actGirlHangG3M(GObj *volatile self)
{
    ACTWay_SetBeginPositionIllegal(self);
    for (;;) {
        if (!PAIR_IsStatus_BOY_DITCH()) {
            ACTSendMailCorrect((void *)self, 0x194);
        }
        _ACTWait(1);
    }
}

inline void actGirlStand(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter actGirlStand\n");
    sub->actMode = 1;
    _ACTWait(0);
}

inline void actGirlWalk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter actGirlWalk\n");
    sub->actMode = 2;
    _ACTWait(0);
}

inline void actGirlRun(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter actGirlRun\n");
    sub->actMode = 3;
    _ACTWait(0);
}

inline void actGirlJump(GObj *volatile self)
{
    char *g = (char *)self;
    Act *s = GOBJ_ACT(g);
    debug_StdPrintfDummy("enter actGirlJump\n");
    s->actMode = 4;
    _ACTWait(0);
}

inline void actGirlAttack(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter actGirlAttack\n");
    sub->actMode = 15;
    _ACTWait(0);
}

inline void actGirlHang(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    int rope;
    /* rope-hang detected from the ACT parameter block */
    int hang = 0;

    if (((int *)GOBJ_WORK(self)->modeHist)[1] == 0x5A) {
        hang = ((int *)GOBJ_WORK(self)->modeHist)[0] == 4;
    }
    /* the debug/free camera forces the pull mail on regardless */
    rope = hang;
    if (girlControlMode != 0) {
        if (((int *)GOBJ_WORK(self)->modeHist)[0] == 4) {
            rope = 1;
            ACTAdjustPlane((void *)self, &GOBJ_WORK(self)->intrReq.a.wall);
        }
    }
    for (;;) {
        if (rope && (60 - systemStatus[0] * 10) / systemStatus[1] * 5 < s->modeFrame) {
            ACTSendMailCorrect((void *)self, 0xC7);
        }
        _ACTWait(1);
    }
}

inline void actGirlBHang(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    int rope = 0;

    if (((int *)GOBJ_WORK(self)->modeHist)[1] == 0x5A) {
        rope = ((int *)GOBJ_WORK(self)->modeHist)[0] == 4;
    }
    ACTAdjustPlane((void *)self, &GOBJ_WORK(self)->intrReq.a.wall);
    for (;;) {
        if (rope && (60 - systemStatus[0] * 10) / systemStatus[1] * 5 < s->modeFrame) {
            ACTSendMailCorrect((void *)self, 0xC7);
        }
        ACTSendMailCorrect((void *)self, 0x150);
        _ACTWait(1);
    }
}

inline void actGirlBecall(GObj *volatile self)
{
    int i;

    for (i = 0; i < (60 - systemStatus[0] * 10) / systemStatus[1] * 2; i++) {
        _ACTLookTarget_Set((void *)self, boyGObj, 0, 5, 1);
        _ACTWait(1);
    }
    ACTSendMailCorrect((void *)self, 252);
    _ACTWait(0);
}

inline void actGirlAttractAction(GObj *volatile self)
{
    for (;;) {
        ACTSendMailCorrect((void *)self, 340);
        _ACTWait(1);
    }
}

inline void actGirlBehanged(GObj *volatile self)
{
    float q[4];

    for (;;) {
        memset(q, 0, 0x10);
        q[3] = 1.0f;
        RotQuaternionY(q, 0);
        SetMotionNodeFixModeParameter(girlGObj, boyGObj, 2, 6, q, 0.0f, 0.0f, 0.0f, 1.0f);
        _ACTWait(1);
    }
}

void actGirlReadyMove(GObj *volatile self)
{
    float dst[4];
    float dir[4];
    int n;

    n = (int)((float)((60 - systemStatus[0] * 10) / systemStatus[1]) * 80.0f / 60.0f);
    dst[0] = GOBJ_ACT(self)->enemy->readyPosX;
    dst[1] = GOBJ_ACT(self)->enemy->readyPosY;
    dst[2] = GOBJ_ACT(self)->enemy->readyPosZ;
    dir[0] = GOBJ_ACT(self)->enemy->readyDirX;
    dir[1] = GOBJ_ACT(self)->enemy->readyDirY;
    dir[2] = GOBJ_ACT(self)->enemy->readyDirZ;
    dst[1] = test_CURRENTROOT((void *)self)[1];
    StartCorrectPosition((void *)self, dst, dir, 1, (float)n);
    while (IsCorrectPosition((void *)self)) {
        debug_Arrow(100.0f, test_CURRENTROOT((void *)self), dir, 0xFF, 0, 0xFF);
        ContinueCorrectPosition((void *)self);
        _ACTWait(1);
    }
    while (1) {
        ACTSendMailCorrect((void *)self, 0x10C);
        _ACTWait(1);
    }
}

void actGirlRescueDst(GObj *volatile self)
{
    float dir[4];
    float q[4];
    float pos[4];
    float p1[4];
    float p2[4];
    float dst[4];
    Act *s;
    EnemyBattleWork *w;
    int n;
    int done;

    s = GOBJ_ACT(self);
    memset(q, 0, 16);
    n = ((60 - systemStatus[0] * 10) / systemStatus[1]) / 3;
    done = 0;
    s->after = (void *)afterGirlHand;
    ACTGame_ConnectHand();
    ACTGameCollisionOn((void *)self);
    gflagOff(393);
    pos[0] = test_CURRENTROOT((void *)self)[0];
    pos[1] = test_CURRENTROOT((void *)self)[1];
    pos[2] = test_CURRENTROOT((void *)self)[2];
    pos[1] = GOBJ_ACT(((int *)boyGObj))->enemy->rescueGirlPos[1];
    ACTSetPositionWithFitting((void *)self, pos);
    w = GOBJ_ACT(((int *)boyGObj))->enemy;
    _OrientXZGV(q, w->rescueBoyPos, w->rescueGirlPos);
    sceVu0SubVector(dir, GOBJ_ACT(((int *)boyGObj))->enemy->rescueGirlPos,
                    test_CURRENTROOT((void *)self));
    sceVu0ScaleVector(dir, dir, 1.0f / (float)n);
    SetMotionDirection((void *)self, q);
    while (1) {
        if (!ACTGame_CheckHandMotion(boyGObj, girlGObj)) {
            ACTGame_DisconnectHand();
            done = 1;
        }
        if (!done && ((60 - systemStatus[0] * 10) / systemStatus[1]) / 2 < s->modeFrame) {
            GetSkeltonPosition(p1, (boyGObj), 6);
            GetSkeltonPosition(p2, (void *)girlGObj, 22);
            if (!(_DistSqGV(p1, p2) < 400.0f)) {
                iosOmSendMail(boyGObj, 248, isysCurrentGObj);
            }
            ACTSendMailCorrect((void *)self, 198);
        }
        if (n > 0) {
            sceVu0AddVector(dst, test_CURRENTROOT((void *)self), dir);
            dst[1] = test_CURRENTROOT((void *)self)[1];
            ACTSetPositionWithFitting((void *)self, dst);
        }
        iosOmSendMail(boyGObj, 350, isysCurrentGObj);
        n--;
        ACTSendMailCorrect((void *)self, 199);
        _ACTWait(1);
    }
}

inline void actGirlSupportGBBegin(GObj *volatile self)
{
    float girl[4];
    float boy[4];
    float dst[4];

    for (;;) {
        girl[0] = test_CURRENTROOT((void *)girlGObj)[0];
        girl[1] = test_CURRENTROOT((void *)girlGObj)[1];
        girl[2] = test_CURRENTROOT((void *)girlGObj)[2];
        boy[0] = test_CURRENTROOT((boyGObj))[0];
        boy[1] = test_CURRENTROOT((boyGObj))[1];
        boy[2] = test_CURRENTROOT((boyGObj))[2];
        boy[1] = girl[1];
        _MoveGV(dst, girl, boy, 20.0f);
        SetDirectRootPositionNoFitting((void *)self, dst);
        ACTSendMailCorrect((void *)self, 0x17E);
        _ACTWait(1);
    }
}

/* the status-range test inside actGirlSupportGBLoop's loop */
static inline unsigned char isGirlSupportGBStatus(void) /* derived name */
{
    unsigned int st = (unsigned int)GOBJ_ACT(((int *)boyGObj))->actMode;

    if (st < 0x6B) {
        if (0x68 <= st) {
            return 1;
        }
    }
    return 0;
}

inline void actGirlSupportGBLoop(GObj *volatile self)
{
    for (;;) {
        if (!isGirlSupportGBStatus()) {
            ACTSendMailCorrect((void *)self, 0x180);
        }
        _ACTWait(1);
    }
}

inline void actGirlSupportGBEnd(GObj *volatile self)
{
    for (;;) {
        ACTSendMailCorrect((void *)self, 199);
        _ACTWait(1);
    }
}

inline void afterGirlSupportBGBegin(ICO_WORD_PTR(GObj *) self)
{
    ICO_WORD_PTR(GObj *) volatile local = self;
    ACTGame_DisconnectHand();
}

void actGirlSupportBGBegin(GObj *volatile self)
{
    float v1[4];
    float v2[4];
    float dst[4];
    float q[4];
    Act *s;
    int i;

    i = 0;
    s = GOBJ_ACT(self);
    v1[0] = test_CURRENTROOT((boyGObj))[0];
    v1[1] = test_CURRENTROOT((boyGObj))[1];
    v1[2] = test_CURRENTROOT((boyGObj))[2];
    v2[0] = test_CURRENTROOT((void *)girlGObj)[0];
    v2[1] = test_CURRENTROOT((void *)girlGObj)[1];
    v2[2] = test_CURRENTROOT((void *)girlGObj)[2];
    memset(q, 0, 0x10);
    q[3] = 1.0f;
    RotQuaternionY(q, 0);
    SetMotionNodeFixModeParameter(girlGObj, boyGObj, 2, 6, q, 0.0f, 0.0f, 0.0f, 1.0f);
    s->after = (void *)afterGirlSupportBGBegin;
    ACTGame_ConnectHand();
    while (1) {
        if (i++ < 6) {
            _InterGV(dst, v1, v2, (float)i, (float)(5 - i));
        }
        ACTSendMailCorrect((void *)self, 0x184);
        _ACTWait(1);
    }
}

int girlcalled;

void actGirlStart(void *self)
{
    char *p;

    girlcalled = 0;
    GirlInfo[1] = 0;
    GirlInfo[0] = (60 - systemStatus[0] * 10) / systemStatus[1] * 30;
    debug_StdPrintfDummy("actGirlStart:%p\n", self);
    p = (char *)actInitialize(self);
    actInitialize_ext_charcter(self);
    actInitialize_only_charcter(self);
    actInitialize_geo(self);
    GOBJ_ACT(self)->enemy->sofaWakeTime =
        (int)(_ACTGame_GetParamF(0x22) * (float)((60 - systemStatus[0] * 10) / systemStatus[1]) /
              60.0f);
    ACTGame_LwsEffectInit(self);
    ACTLookTarget_Init(self);
    ICO_RAW(int, p, 0x180, ((Act *)p)->heldItem.i) = 0;
    ICO_RAW(int, p, 0x184, ((Act *)p)->nextItem.i) = 0;
    ACTParaStatus_Init(self);
    _ACTCharStatus_Init(self);
    _ACTWait(1);
    ACTGameView_FirstSet(self);
    brainInitGirlSet(self, boyGObj);
    if (debug_brain_flag != 0) {
        actCreateSubThread(subGirlBrainMain, 20);
    }
    ICO_RAW(IntrMail *, p, 0xD0, ((Act *)p)->mainMail) = (IntrMail *)&actIntrList[74];
    actCreateSubThread(subGirlControl, 21);
    actCreateSubThread(subGirlCollision, 21);
    actCreateSubThread(subCommonIdle, 21);
    ICO_RAW(IntrMail *, p, 0xD4, ((Act *)p)->mail) = (IntrMail *)&actIntrList[79];
    ICO_RAW(int, p, 0x350, ((Act *)p)->wayMode) = 0;
    ICO_RAW(float, p, 0x1E0, ((Act *)p)->life) = 100.0f;
    ICO_RAW(int, p, 0x48, ((Act *)p)->actKind) = 1;
    ACTSendMailCorrect(self, 0xC7);
    _ACTWait(0);
}

void GirlAct_BoyAndMeCollisionMail(void *self)
{
    float vec[4];
    float boyPos[4];
    float myPos[4];
    float ang;

    ACTSendMailCorrect(self, 0x10D);

    if (GOBJ_ACT(boyGObj)->actMode == 1) {
        return;
    }
    GetRootPosition(boyPos, boyGObj);
    GetRootPosition(myPos, self);

    sceVu0SubVector(vec, boyPos, myPos);
    sceVu0Normalize(vec, vec);

    ang = _RotyGV(vec, test_CURRENTORIENT(self));

    if ((ang < 0.0f ? -ang : ang) < 45.0f) {
        ACTSendMailCorrect(self, 0x10E);
        return;
    } else if ((ang < 0.0f ? -ang : ang) > 135.0f) {
        ACTSendMailCorrect(self, 0x10F);
        return;
    } else if (ang > 45.0f) {
        ACTSendMailCorrect(self, 0x110);
        return;
    } else {
        ACTSendMailCorrect(self, 0x111);
    }
}

static inline unsigned char isGirlEscortStatus(void) /* derived name */
{
    Act *s = GOBJ_ACT(girlGObj);
    int mode = s->actMode;
    const ActModeRec *attr = &actModeTbl[mode];
    if (attr->escort && mode != 0x6F && ((int)(s->flags20.ll >> 46) & 1)) {
        return 1;
    }
    return 0;
}

static inline const EscortPoint *searchEscortPoint(int area, int point) /* derived name */
{
    const EscortPoint *p;
    int i;
    for (i = 0; i < 98; i++) {
        p = &autoEscortData[i];
        if (p->area == area && p->point == point) {
            return p;
        }
    }
    return 0;
}

int IsGirlStatusEscortEnable(int area, int point)
{
    float v[4];
    const EscortPoint *p;
    float d;

    if (((int *)boyGObj) != 0 && (void *)girlGObj != 0 && isGirlEscortStatus()) {
        p = searchEscortPoint(area, point);
        if (p != 0) {
            v[0] = -p->pos[0];
            v[1] = -p->pos[1];
            v[2] = -p->pos[2];
            d = _DistGV(v, test_CURRENTROOT((void *)girlGObj));
            if (d < p->range) {
                GOBJ_WORK(girlGObj)->escortOffset = d * p->rate;
                return 1;
            }
        } else {
            if (_DistSqGV(test_CURRENTROOT((boyGObj)), test_CURRENTROOT((void *)girlGObj)) <
                _ACTGame_GetParamF(5) * _ACTGame_GetParamF(5)) {
                return 1;
            }
        }
    }
    return 0;
}

inline int isMustCheckCylinder(void *a, void *b)
{
    if ((a == (void *)((int *)boyGObj) && b == (void *)girlGObj) ||
        (a == (void *)girlGObj && b == (void *)((int *)boyGObj))) {
        if (GOBJ_ACT(girlGObj)->actMode == 0x51) {
            return 1;
        }
    }
    return 0;
}

static inline void dispEscortSphere(void *pos, float r, unsigned char in) /* derived name */
{
    Col4 col;

    if (debug_disp_escort_ball) {
        MatrixDrive_PushMatrix();
        /* the wire sphere colour: a GNU constructor expression, which gcc keeps
           as an anonymous .rodata constant and copies here */
        col = (Col4){{0, 0x10, 0x20, 0x80}};
        if (in) {
            col.c[0] = 0xFF;
        }
        gif_StartPacketPri(0xB);
        gif_SetZTest(1);
        sceVu0UnitMatrix(MatrixDrive_GetMatrix());
        MatrixDrive_TransMatrixV(pos);
        prim_DispWireSphere(r, &col, 16, 8);
        gif_EndPacket();
        MatrixDrive_PopMatrix();
    }
}

void DebugDispAutoEscort(void)
{
    Vec4 pos = {{3.40282347e+38f, 0.0f, 0.0f, 1.0f}}; /* FLT_MAX: no girl yet */
    Vec4 v;
    const EscortPoint *p;
    int i;
    int in;

    if (debug_disp_escort_ball == 0) {
        return;
    }
    if ((void *)girlGObj != 0 && isGirlEscortStatus()) {
        pos.f[0] = test_CURRENTROOT((void *)girlGObj)[0];
        pos.f[1] = test_CURRENTROOT((void *)girlGObj)[1];
        pos.f[2] = test_CURRENTROOT((void *)girlGObj)[2];
    }
    for (i = 1; i < 16; i++) {
        p = searchEscortPoint(stage_no, i);
        if (p != 0) {
            v.f[0] = -p->pos[0];
            v.f[1] = -p->pos[1];
            v.f[2] = -p->pos[2];
            in = _DistSqGV(&v, &pos) < p->range * p->range;
            dispEscortSphere(&v, p->range, in);
        }
    }
}

/* the *(sub+0x688) ACT parameter block, viewed as a struct whose stores are
   member stores */
typedef struct { /* field names derived */
    char pad0[944];
    int turnMailWait; /* 0x3B0 : ActWork's turnMailWait, set by "cannot reach" */
    char pad3B4[364];
    float hintPosX; /* 0x520 : hint-point target position */
    float hintPosY;
    float hintPosZ;
} ActPara; /* derived name */

static inline void afterGirlHintPoint(GObj *volatile self)
{
    RequestChangeHandMode(self, 1, 4, 0, 0, 0, 0);
}

void actGirlHintPoint(GObj *volatile self)
{
    float d[4];
    float p[4];
    float q[4];
    float r[4];
    float u[4];
    float o1[4];
    float o2[4];
    void *tgt;
    Act *s;

#ifdef ICO_HOST
    tgt = (void *)GOBJ_ACT(self)->intrData;
#else
    tgt = (void *)*(int *)(GOBJ_ACT(self)->intrData);
#endif
    s = GOBJ_ACT(self);
#ifdef ICO_HOST
    ACT_AFTER_PROC(s) = (void (*)(GObj *))afterGirlHintPoint;
#else
    *(void **)((char *)s + 0x18) = (void *)afterGirlHintPoint;
#endif
    sceVu0SubVector(d, test_CURRENTROOT(tgt), test_CURRENTROOT((void *)self));
    while (1) {
        sceVu0AddVector(p, test_CURRENTROOT((void *)self), d);
        sceVu0SubVector(p, p, test_CURRENTROOT(tgt));
        RequestChangeHandMode(self, 1, 4, 3, tgt, 0, p);
        if (((int *)boyGObj)) {
            q[0] = test_CURRENTROOT((void *)self)[0];
            q[1] = test_CURRENTROOT((void *)self)[1];
            q[2] = test_CURRENTROOT((void *)self)[2];
            r[0] = test_CURRENTROOT((boyGObj))[0];
            r[1] = test_CURRENTROOT((boyGObj))[1];
            r[2] = test_CURRENTROOT((boyGObj))[2];
            sceVu0AddVector(u, q, d);
            _OrientXZGV(o1, r, q);
            _OrientXZGV(o2, u, q);
            if (_AbsRotyGV(o1, o2) >= 121) {
                _ACTCharStatus_Set((void *)self, 13, -1.0f, 0);
                ((ActPara *)((char *)GOBJ_ACT(self)->work))->hintPosX = u[0];
                ((ActPara *)((char *)GOBJ_ACT(self)->work))->hintPosY = u[1];
                ((ActPara *)((char *)GOBJ_ACT(self)->work))->hintPosZ = u[2];
            }
        }
        _ACTCharStatus_Set((void *)self, 14, -1.0f, 0);
        ACTSendMailCorrect((void *)self, 199);
        _ACTWait(1);
    }
}

inline void actGirlHintVoice(GObj *volatile self)
{
    for (;;) {
        _ACTCharStatus_Set((void *)self, 14, -1.0f, 0);
        ACTSendMailCorrect((void *)self, 199);
        _ACTWait(1);
    }
}

inline void actGirlCannotReach(GObj *volatile self)
{
    for (;;) {
        ((ActPara *)((char *)GOBJ_ACT(self)->work))->turnMailWait =
            (60 - systemStatus[0] * 10) / systemStatus[1] * 10;
        ACTSendMailCorrect((void *)self, 199);
        _ACTWait(1);
    }
}
