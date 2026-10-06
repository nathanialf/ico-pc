#include "ee_view.h"
#include "typedef.h"
#include "boyact.h"
#include "debug.h"
#include "gamesys.h"
#include "sceneManager.h"
#include "gobj.h"
#include "obj_manager.h"
#include "girl_act.h"
#include "brain.h"
#include "gflag.h"
#include "layout_action.h"
#include "frameDependSequence.h"
#include "torch.h"
#include <string.h>
#include "geometryManager.h"
#include "chain.h"
#include "act-game.h"
#include "motionOrientManager.h"
#include "motionFileManager.h"
#include "matrixDrive.h"
#include "debug_exception.h"
#include "layout_texture.h"
#include "motionManager.h"
#include "main.h"
#include "enemy_act.h"
#include <libvu0.h>
#include "cage.h"
#include "isys.h"
#include "way_sys.h"
#include "fuzio.h"
#include "pad.h"
#include "weapon.h"
#include "StageAnimation.h"
#include "attackhit.h"
#include "mail-add-data.h"
#include "camera-editor.h"
#include "quaternion.h"
#include "script.h"
#include "commonact.h"
#include "act.h"
#include "gv.h"
#include <assert.h>
#include "poly-flat.h"
#include "fieldCollision.h"
#include "motionManager2.h"
#include "box.h"

typedef struct { /* field names derived */
    int a, b, c;
} S12; /* derived name */

/* One word of the boy's actor parameter block at gobj->x15C: the motion code
   writes these slots as float and the evaluator reads them as int, so the word
   itself is a union. */
typedef union BoyVal { /* field names derived */
    int i;
    float f;
} BoyVal; /* derived name */

/* motionOrientManager.h declares none of the motion tables */
extern MotionDef motionKind[];

/* the motion-def row of an actor's current motion */
#define CHAINROW(self) (GOBJ_SUB(self)->ctrl.motion + motionKind) /* derived name */

static void findChainInJump(void *self)
{
    float p[4];
    float q[4];
    float w[4];
    float sk[4];
    float rt[4];
    float lp[4];
    float lv[4];
    float mtx[16];
    float hp0[4];
    float hp1[4];
    float cp[4];
    float ce[4];
    float pos[4];
    float hx[4];
    float cp2[4];
    float ce2[4];
    Act *sub;
    void *g;
    void *cage;
    float r;
    float r2;
    float ang;
    int rside = 0;
    int lside = 0;

    sub = GOBJ_ACT(self);
    r = ((CHAINROW(self)->flags2.word >> 8) & 1) ? 300.0f : 100.0f;
    r2 = ((CHAINROW(self)->flags2.word >> 8) & 1) ? 90.0f : 120.0f;

    for (g = isysGObjSearchFromObjKindID_begin(0x15); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        if (((GObj *)g)->active != 0) {
            GetRootPosition(p, g);
            q[0] = p[0];
            q[1] = p[1];
            q[2] = p[2];
            q[1] += GetChainLength(g) + 50.0f;
            if (_DistxzSqGV(test_CURRENTROOT(self), p) < r * r &&
                p[1] < test_CURRENTROOT(self)[1] && test_CURRENTROOT(self)[1] < q[1]) {
                _OrientXZGV(rt, p, test_CURRENTROOT(self));
                ang = (float)_RotyGV(rt, test_CURRENTORIENT(self));
                if ((ang < 0.0f ? -ang : ang) < r2) {
                    if (sub->actMode == 4) {
                        if (0.0f < ang) {
                            rside = 1;
                        } else {
                            lside = 1;
                        }
                    }
                }
                GetSkeltonPosition(sk, self, 18);
                w[0] = 0.0f;
                w[1] = sk[1] - p[1];
                w[2] = 0.0f;
                break;
            }
        }
    }

    rt[0] = test_CURRENTROOT(self)[0];
    rt[1] = test_CURRENTROOT(self)[1];
    rt[2] = test_CURRENTROOT(self)[2];
    GetMatrixDirectionToZ(mtx, test_CURRENTORIENT(self));
    lp[0] = p[0];
    lp[1] = p[1];
    lp[2] = p[2];
    sceVu0SubVector(lv, (float *)lp, (float *)rt);
    lv[3] = 0.0f;
    sceVu0ApplyMatrix(lv, mtx, lv);

    if (((CHAINROW(self)->flags2.word >> 6) & 1) == 0 && (rside != 0 || lside != 0) &&
        (lv[2] < 0.0f ? -lv[2] : lv[2]) < 150.0f) {
        RequestChangeHandMode(self, 0, 2, 1, g, 0, w);
        RequestChangeHandMode(self, 1, 2, 1, g, 0, w);
    } else {
        RequestChangeHandMode(self, 0, 2, 0, 0, 0, 0);
        RequestChangeHandMode(self, 1, 2, 0, 0, 0, 0);
    }

    if ((rside != 0 || lside != 0) && ((CHAINROW(self)->flags2.word >> 5) & 1) != 0) {
        float sp = (systemStatus[0] == 1) ? 0.5f : 0.8f;

        ((BoyVal *)&GOBJ_SUB(self)->root.ikRate0)->f = sp;
        ((BoyVal *)&GOBJ_SUB(self)->root.ikRate1)->f = sp;
        ((BoyVal *)&GOBJ_SUB(self)->root.ikRate2)->f = sp;
    }

    if (rside != 0 || lside != 0) {
        _ACTCharStatus_Set(self, 18, -1.0f, (ICO_WORD)g);
    }

    if (rside != 0) {
        GetSkeltonPosition(hp0, self, 22);
        if ((lv[0] < 0.0f ? -lv[0] : lv[0]) < 60.0f && (lv[2] < 0.0f ? -lv[2] : lv[2]) < 100.0f) {
            iosOmSendMail(self, 20, g);
        }
    }

    if (lside != 0) {
        GetSkeltonPosition(hp1, self, 6);
        if ((lv[0] < 0.0f ? -lv[0] : lv[0]) < 60.0f && (lv[2] < 0.0f ? -lv[2] : lv[2]) < 100.0f) {
            iosOmSendMail(self, 20, g);
        }
    }

    if (g != 0 && sub->actMode == 65) {
        GetSkeltonPosition(hp0, self, 6);
        GetChainNearestNodePosition(hp1, g, hp0);
        if ((hp0[1] - hp1[1] < 0.0f ? -(hp0[1] - hp1[1]) : (hp0[1] - hp1[1])) < 5.0f) {
            iosOmSendMail(self, 20, g);
        }
    }

    if (sub->actMode == 65) {
        void *o;

        cage = 0;
        pos[0] = test_CURRENTROOT(self)[0];
        pos[1] = test_CURRENTROOT(self)[1];
        pos[2] = test_CURRENTROOT(self)[2];
        for (o = isysGObjSearchFromObjKindID_begin(0x2C); o != 0;
             o = isysGObjSearchFromObjKindID_next(o)) {
            if (((GObj *)o)->active != 0) {
                if (GetCageChainPoint(cp, ce, o) != 0) {
                    if (_DistxzSqGV(test_CURRENTROOT(self), cp) < 4.9e+03f && ce[1] > pos[1]) {
                        cage = o;
                        break;
                    }
                }
            }
        }
        if (cage != 0) {
            GetRootPositionHandExtra(self, hx);
            GetCageChainPoint(cp2, ce2, cage);
            if (cp2[1] + 150.0f < hx[1]) {
                ACTSendMailCorrect(self, 0xAE);
                ACTSendMailCorrect(self, 0xAD);
            }
        }
    }
}

/* dir: subBoyCollision passes the motion direction ((char *)sub->dir); this body
   never reads it */
static int CorrectOrient_RopeCliff(float *out, void *gobj, float *dir)
{
    float pos[4];
    float rpos[4];
    float orient[4];
    float climb[4];
    Act *sub = GOBJ_ACT(gobj);
    void *p;
    int r;
    int a;
    int b;
    float dy;

    switch (sub->actMode) {
    case 3:
        r = 0x78;
        break;
    case 2:
        r = 0x5A;
        break;
    default:
        return 0;
    }
    GetRootProjectionPosOfGObj(pos, gobj);
    p = isysGObjSearchFromObjKindID_begin(21);
    while (p != 0) {
        if (((GObj *)p)->active != 0) {
            GetRootPosition(rpos, p);
            if (_DistxzSqGV(pos, rpos) < (float)(r * r)) {
                dy = pos[1] - rpos[1];
                if ((dy < 0.0f ? -dy : dy) < 70.0f) {
                    _OrientXZGV(orient, rpos, pos);
                    a = _AbsRotyGV((char *)sub->dir, orient);
                    GetChainClimbOrient(climb, p);
                    b = _AbsRotyGV((char *)sub->dir, climb);
                    if (a < 0x4B && b < 0x4B) {
                        out[0] = orient[0];
                        out[1] = orient[1];
                        out[2] = orient[2];
                        return 1;
                    }
                }
            }
        }
        p = isysGObjSearchFromObjKindID_next(p);
    }
    return 0;
}

/* The three climb headers (omori/include/b50climb.h, b100climb.h,
   b200climb.h) textually included here, as girl_act.c does with its own
   three: each defines the hand-off's after-routine and act-routine `inline`,
   which the compiler emits at the end of the file in boyact.h's order, and
   the mot-routine plainly, emitted in place.  Their strings come out here,
   in this order, at the head of the TU's .rodata. */
inline void afterBoyHand50(GObj *volatile self)
{
    debug_StdPrintfDummy("boy after func\n");
    if (girlGObj != 0) {
        iosOmSendMail(girlGObj, 0x60, isysCurrentGObj);
    }
}

inline void actBoyHand50(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter actBoyHand50\n");
    sub->actMode = 0x52;
    sub->after = (void *)afterBoyHand50;
    sub->readyFlags = 0;
    while ((sub->readyFlags & 0x10) == 0) {
        _ACTWait(1);
    }
    debug_StdPrintfDummy("boy error flg get\n");
    while (1) {
        ACTSendMailCorrect(self, 0x60);
        _ACTWait(1);
    }
}

void motBoyHand50(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter motBoyHand50\n");
    while (1) {
        if (girlGObj != 0) {
            iosOmSendMail(girlGObj, 0x5C, isysCurrentGObj);
        }
        if (sub->readyFlags & 1) {
            break;
        }
        _ACTWait(1);
    }
    while (GOBJ_SUB(self)->ctrl.motion < 0 || 2 <= GOBJ_SUB(self)->ctrl.motion) {
        _ACTWait(1);
    }
    _ACTWait(1);
    while (1) {
        if (girlGObj != 0) {
            iosOmSendMail(girlGObj, 0x5D, isysCurrentGObj);
        }
        if (sub->readyFlags & 2) {
            break;
        }
        _ACTWait(1);
    }
    _ACTWait(45);
    sub->after = 0;
    while (1) {
        ACTSendMailCorrect(self, 0x47);
        _ACTWait(1);
    }
}

/* the shared "face the girl" prologue the b100climb.h / b200climb.h climb
   motions open with */
static inline void faceGirlFlat(void) /* derived name */
{
    float dir[4];
    void *boy = boyGObj;

    sceVu0SubVector(dir, test_CURRENTROOT(boyGObj), test_CURRENTROOT(girlGObj));
    dir[1] = 0.0f;
    sceVu0Normalize(dir, dir);
    SetMotionDirection(boy, dir);
}

inline void afterBoyHand100(GObj *volatile self)
{
    debug_StdPrintfDummy("boy after func\n");
    if (girlGObj != 0) {
        iosOmSendMail(girlGObj, 0x65, isysCurrentGObj);
    }
}

inline void actBoyHand100(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter actBoyHand100\n");
    sub->actMode = 0x53;
    sub->after = (void *)afterBoyHand100;
    sub->readyFlags = 0;
    while ((sub->readyFlags & 0x10) == 0) {
        _ACTWait(1);
    }
    debug_StdPrintfDummy("boy error\n");
    while (1) {
        ACTSendMailCorrect(self, 0x65);
        _ACTWait(1);
    }
}

void motBoyHand100(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    int n;

    debug_StdPrintfDummy("enter motBoyHand100\n");
    faceGirlFlat();
    n = (60 - systemStatus[0] * 10) / systemStatus[1] / 2;
    while (1) {
        if (0 < n) {
            n--;
            if (girlGObj != 0) {
                iosOmSendMail(girlGObj, 0x61, isysCurrentGObj);
            }
            if ((sub->readyFlags & 1) == 0) {
                goto cont;
            }
            goto done;
        }
        break;
    cont:
        _ACTWait(1);
    }
    ACTSendMailCorrect(self, 0x65);
    debug_StdPrintfDummy("%s sync error\n", (void *)self == boyGObj ? "boy" : "girl");
done:
    while (GOBJ_SUB(self)->ctrl.motion < 0 || 2 <= GOBJ_SUB(self)->ctrl.motion) {
        _ACTWait(1);
    }
    _ACTWait(1);
    sub->motReq = SetMotionRequest((void *)self, 0x65, sub->env.motOriReq);
    sub->motReq = SetMotionRequest((void *)self, 0xA4, sub->env.motOriReq);
    while ((((struct MotCtrl *)sub->motReq)->frameEnd & 1) == 0) {
        _ACTWait(1);
    }
    while (1) {
        if (girlGObj != 0) {
            iosOmSendMail(girlGObj, 0x62, isysCurrentGObj);
        }
        if (sub->readyFlags & 2) {
            break;
        }
        _ACTWait(1);
    }
    sub->motReq = SetMotionRequest((void *)self, 0x65, sub->env.motOriReq);
    while ((((struct MotCtrl *)sub->motReq)->frameEnd & 1) == 0) {
        _ACTWait(1);
    }
    while (1) {
        if (girlGObj != 0) {
            iosOmSendMail(girlGObj, 0x64, isysCurrentGObj);
        }
        if (sub->readyFlags & 8) {
            break;
        }
        _ACTWait(1);
    }
    sub->after = 0;
    while (1) {
        ACTSendMailCorrect(self, 0x47);
        _ACTWait(1);
    }
}

inline void afterBoyHand200(GObj *volatile self)
{
    debug_StdPrintfDummy("boy after func\n");
    if (girlGObj != 0) {
        iosOmSendMail(girlGObj, 0x6A, isysCurrentGObj);
    }
}

inline void actBoyHand200(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter actBoyHand200\n");
    sub->actMode = 0x54;
    sub->after = (void *)afterBoyHand200;
    sub->readyFlags = 0;
    while ((sub->readyFlags & 0x10) == 0) {
        _ACTWait(1);
    }
    debug_StdPrintfDummy("boy error\n");
    while (1) {
        ACTSendMailCorrect(self, 0x6A);
        _ACTWait(1);
    }
}

void motBoyHand200(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    int n;

    debug_StdPrintfDummy("enter motBoyHand200\n");
    faceGirlFlat();
    n = (60 - systemStatus[0] * 10) / systemStatus[1] / 2;
    while (1) {
        if (0 < n) {
            n--;
            if (girlGObj != 0) {
                iosOmSendMail(girlGObj, 0x66, isysCurrentGObj);
            }
            if ((sub->readyFlags & 1) == 0) {
                goto cont;
            }
            goto done;
        }
        break;
    cont:
        _ACTWait(1);
    }
    ACTSendMailCorrect(self, 0x6A);
    debug_StdPrintfDummy("%s sync error\n", (void *)self == boyGObj ? "boy" : "girl");
done:
    while (GOBJ_SUB(self)->ctrl.motion < 0 || 2 <= GOBJ_SUB(self)->ctrl.motion) {
        _ACTWait(1);
    }
    _ACTWait(1);
    sub->motReq = SetMotionRequest((void *)self, 0x66, sub->env.motOriReq);
    while ((((struct MotCtrl *)sub->motReq)->frameEnd & 1) == 0) {
        _ACTWait(1);
    }
    sub->motReq = SetMotionRequest((void *)self, 0x66, sub->env.motOriReq);
    sub->motReq = SetMotionRequest((void *)self, 0xA4, sub->env.motOriReq);
    while ((((struct MotCtrl *)sub->motReq)->frameEnd & 1) == 0) {
        _ACTWait(1);
    }
    while (1) {
        if (girlGObj != 0) {
            iosOmSendMail(girlGObj, 0x67, isysCurrentGObj);
        }
        if (sub->readyFlags & 2) {
            break;
        }
        _ACTWait(1);
    }
    sub->motReq = SetMotionRequest((void *)self, 0x66, sub->env.motOriReq);
    while ((((struct MotCtrl *)sub->motReq)->frameEnd & 1) == 0) {
        _ACTWait(1);
    }
    while (1) {
        if (girlGObj != 0) {
            iosOmSendMail(girlGObj, 0x69, isysCurrentGObj);
        }
        if (sub->readyFlags & 8) {
            break;
        }
        _ACTWait(1);
    }
    sub->after = 0;
    while (1) {
        ACTSendMailCorrect(self, 0x47);
        _ACTWait(1);
    }
}

/* One 0x50-byte record per act status, indexed by sub->0x34. */

/* Each of the two height tests is one source line applying ABSF twice to the
   same height difference, so each re-evaluates the pair of test_CURRENTROOT
   calls.  The second test's body is empty in retail. */
#define BOYGIRL_DY()                                                                               \
    (test_CURRENTROOT(boyGObj)[1] - test_CURRENTROOT(girlGObj)[1]) /* derived name */
#define ABSF(x) ((x) < 0.0f ? -(x) : (x))                          /* derived name */

void handoff_heroin(void)
{
    void *boy = boyGObj;

    if (girlGObj != 0) {
        if (GOBJ_SUB(girlGObj)->root.hand0.mode == 6) {
            if (actModeTbl[GOBJ_ACT(boy)->actMode].bit7) {
            } else {
                iosOmSendMail(girlGObj, 0x3E, isysCurrentGObj);
            }
            ACTSendMailCorrect(boy, 0xFA);
        } else if (_DistxzGV(test_CURRENTROOT(boy), test_CURRENTROOT(girlGObj)) < 100.0f &&
                   ABSF(ABSF(BOYGIRL_DY())) < 100.0f) {
            GetHeightOfFieldPlaneDifference(boyGObj, girlGObj);
        }
    }
    if (girlGObj != 0 && GOBJ_SUB(girlGObj)->root.hand0.mode != 6) {
        if (_DistxzGV(test_CURRENTROOT(boyGObj), test_CURRENTROOT(girlGObj)) < 100.0f &&
            ABSF(ABSF(BOYGIRL_DY())) < 100.0f) {}
    }
}

static float pinchBoyPos[4]; /* derived name */

static float pinchCameraPos[4]; /* derived name */

static float pinchBoyPosLate[4]; /* derived name */

typedef struct { /* field names derived */
    int a;
    int b;
} CharPos; /* derived name */

/* the wall hit actBoyStart hands the boy and the girl with mail 0x36: the
   ClipWall work record's wall pair and the wall it hit */
static WallCfg sofaWallHit; /* derived name */

static int attrWallHit[3]; /* derived name */

static long long
    boyInfo[24]; /* BoyInfo (below) is wider with 8-byte pointers: 0x60 bytes on the EE */

static void CheckCollisionAttr(void *self)
{
    Sub15C *stage = GOBJ_SUB(self);
    int i;
    int flag = 1;
    int esc = 0;

    if (_ACTGame_GetParamF(2) < stage->ctrl.groundHeight) {
        return;
    }
    if (GOBJ_ACT(self)->actMode == 0x16) {
        return;
    }
    if (gameover_flag != 0) {
        return;
    }
    if (scpBoyControlReadDisable != 0) {
        return;
    }
    for (i = 1; i < 16; i++) {
        if (CheckFloorAttribute(self, i)) {
            int *w = (int *)boyInfo;

            flag = 0;
            if (w[4] < 0) {
                w[4] = i;
                return;
            }
            if (w[4] != i) {
                if (ACTGame_FLAG_TETSUNAGI()) {
                    esc = 1;
                } else if (girlGObj != 0) {
                    if (IsGirlStatusEscortEnable(stage_no, i)) {
                        esc = 1;
                    }
                }
                if (esc) {
                    boyInfo[1] |= 0x800000000LL;
                    RequestStageChange(i, boyGObj, girlGObj, 1.0f, 8.0f);
                } else {
                    RequestStageChange(i, boyGObj, 0, 1.0f, 8.0f);
                }
                itemWatchOff = 1;
            }
        }
    }
    if (flag) {
        ((int *)boyInfo)[4] = 0xFF;
    }
}

typedef struct { /* field names derived */
    int id;      /* 0x00 */
    float f04;
    float f08;
    unsigned char b0C; /* 0x0C */
    unsigned char b0D;
    unsigned char b0E;
    unsigned char b0F;
    float f10; /* 0x10 */
    char pad14[0x20 - 0x14];
    float f20[4]; /* 0x20 */
    float f30[4]; /* 0x30 */
} BgaEntry;       /* derived name */

typedef struct { /* field names derived */
    long long w[12];
} BoyWork; /* derived name */

typedef struct { /* field names derived */
    int w[8];
} BoyKidnapWork; /* derived name */

/* the private insert-camera record, 16-aligned as the programmer's other
   vector records (act-game.c Vec4S, way_sys.c WayClipWork) */
typedef struct {           /* field names derived */
    float pos[4];          /* 0x00, where the camera starts */
    float tgt[4];          /* 0x10, where it moves to */
    struct GObj *track;    /* 0x20, the object the return leg follows, or 0 for pos */
    int inFrames;          /* 0x24, frames of the move to tgt */
    int outFrames;         /* 0x28, frames of the return */
    float inRate;          /* 0x2C, the move's interpolation step */
    float blend;           /* 0x30, InsertCamera_SetDetail's blend */
    unsigned char control; /* 0x34, PrivInsCamChk_Control's answer */
    unsigned char pad35[3];
    int cnt;                               /* 0x38 */
    int on;                                /* 0x3C */
    float cur[4];                          /* 0x40 */
} __attribute__((aligned(16))) PrivInsCam; /* derived name */

static BoyWork boyInfoDefault = {{0, 0, 0xFFFFFFFF}}; /* derived name */

static BoyKidnapWork characterPacketDefault = {{0, 0, 0, 0, 0, -1, -1}}; /* derived name */

static BgaEntry boyBgaTable[] = {
    /* derived name */
    {480, 0.0f, 7.0f, 1, 70, 1, 1, 1.0f},
    {481, 0.0f, 0.0f, 0, 70, 0, 1, 1.0f},
    {485, 0.0f, 0.0f, 0, 0, 0, 0, -1.0f},
    {486, -10.0f, 23.0f, 1, 70, 1, 1, -1.0f},
    {-1},
}; /* derived name */

/* PrivInsCam's initial value: subBoyCollision copies it whole into
   privInsCam. */
static PrivInsCam privInsCamDefault = {
    /* derived name */
    {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, 0, 0, 0, 0.5f, 0.2f}; /* derived name */

/* boyact.o's two .data globals, the last two quadwords of its .data:
   test_rope_velo, which no code names, and add_rope_vec, the boy's orient
   snapshot SaveBoyOrientForScript writes. */
float test_rope_velo[4] = {0.0f, 0.0f, 0.0f, 0.0f};

float add_rope_vec[4] = {0.0f, 0.0f, 0.0f, 0.0f};

static void UpdateGeo(void *self, BgaEntry *p)
{
    float dir[4];
    float tmp[4];
    void *obj;

    if (p->b0F != 0) {
        dir[0] = test_CURRENTORIENT(self)[0];
        dir[1] = test_CURRENTORIENT(self)[1];
        dir[2] = test_CURRENTORIENT(self)[2];
    } else {
        obj = isysGObjSearchFromObjKindID_begin(47);
        _OrientXZGV(dir, test_CURRENTROOT(obj), test_CURRENTROOT(self));
    }
    sceVu0ScaleVector(dir, dir, p->f10);
    ActGame_GetOrientQ(p->f30, dir, 0);
    sceVu0ScaleVector(tmp, test_CURRENTORIENT(self), p->f08);
    sceVu0AddVector(p->f20, test_CURRENTROOT(self), tmp);
    sceVu0ScaleVector(tmp, test_CURRENTORIENT(self), p->f04);
    _ApplyRyGV(tmp, 1.5707964f);
    sceVu0AddVector(p->f20, p->f20, tmp);
    p->f20[1] += (float)p->b0D;
}

static void BoyBgaManager(void *self, int id, void *dst)
{
    BgaEntry *p;
    int i;
    int v;
    int r;

    for (i = 0; 0 <= boyBgaTable[i].id; i++) {
        if (boyBgaTable[i].id == id) {
            p = &boyBgaTable[i];
            goto found;
        }
    }
    debug_assert(__FILE__, 1731);
    __assert(__FILE__, 1731, "0");
    p = 0;
found:
    v = *(int *)dst;
    if (v < 0) {
        return;
    }
    if (p->b0C != 0) {
        UpdateGeo(self, p);
        goto reload;
    }
    if (v == 0) {
        UpdateGeo(self, p);
    reload:
        v = *(int *)dst;
    }
    r = (int)stage_PlayBgAnimation(p->id, (float)v, p->f20, p->f30);
    if (p->b0E == 0) {
        *(int *)dst = r;
    } else if (0 <= r) {
        *(int *)dst = r;
    }
}

static unsigned char girlLookStarted; /* derived name */

static unsigned char girlLookInScreen; /* derived name */

static unsigned char weaponLookStarted; /* derived name */

static unsigned char weaponLookInScreen; /* derived name */

static unsigned char girlEscortedInStage; /* derived name */

static unsigned char ableBoyControl; /* derived name */

static unsigned char startWithGirl; /* derived name */

static unsigned char startWaitCancel; /* derived name */

static unsigned char startOnSofa; /* derived name */

static unsigned char sitLayoutDone; /* derived name */

static int sitCount; /* derived name */

static void *beliftGirl; /* derived name */

static void E3_StageStartBoy(void *self)
{
    float buf[4];
    int w1;
    int w2;
    int w3;

    if (startWithGirl != 0) {
        sceVu0ScaleVector(buf, test_CURRENTORIENT(self), 100.0f);
        sceVu0AddVector(buf, buf, test_CURRENTROOT(self));
        SetDirectRootPositionNoFitting(self, buf);
    }
    if (gflagChk(381)) {
        gflagOff(381);
        return;
    }
    if (GetStageStartInfo(self, 0, 0, &w1, &w2, &w3) == 0) {
        return;
    }
    if (startOnSofa != 0) {
        return;
    }
    if (startWaitCancel != 0) {
        return;
    }
    _ACTWait(3);
    GetStageStartInfo(self, 0, 0, &w1, &w2, &w3);
    _ACTWait(w1);
    scpPlayStart(self);
    scpPlayMotReq(self, 8);
    _ACTWait(w2);
    scpPlayEnd(self);
    _ACTWait(w3);
}

extern int fptodp(float v);

static int GetChainSlope(void)
{
    float a;
    float b;
    float c;
    float ratio;
    GObj *g = boyGObj;
    int up;
    int down;

    GetChainPendulum((void *)GOBJ_ACT(g)->chain, &a, &b, &c);
    if (b < 5.0f) {
        return 0;
    }
    ratio = (float)*motionTable[GOBJ_SUB(g)->ctrl.motion] / (c * 0.5f);
    ACTGame_SetMotionPlaySpeedRatio_Reserve(
        g, ratio * (float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 30.0f, 7);
    if (debug_font_flag & 1) {
        debug_Printf(10, 140, 0x0FFFFFFF, "speed = %f (%f)\n", fptodp(ratio), fptodp(c));
    }
    if (debug_font_flag & 1) {
        debug_Printf(10, 150, 0x0FFFFFFF, "%f / %f\n", fptodp(GOBJ_SUB(g)->ctrl.animFrame),
                     fptodp((float)*motionTable[GOBJ_SUB(g)->ctrl.motion]));
    }
    if ((b < 0.0f ? -b : b) < 30.0f) {
        up = 1;
        down = 2;
    } else {
        up = 3;
        down = 4;
    }
    if (a < 0.0f) {
        return up;
    }
    return down;
}

/* as in camera-root.h, which this TU does not include (GetCurrentCameraSet2 differ) */
extern int InsertCameraWorkingFlag;
/* as in camera-root.h, which this TU does not include (GetCurrentCameraSet2 differ) */
extern int FixViewInGameCameraFlag;

/* An inline-only static that snapshots the boy's orient where the script
   side reads it, inlined into subBoyControl and subBoyCollision. */
static inline void SaveBoyOrientForScript(void) /* derived name */
{
    void *boy = boyGObj;

    add_rope_vec[0] = test_CURRENTORIENT(boy)[0];
    add_rope_vec[1] = test_CURRENTORIENT(boy)[1];
    add_rope_vec[2] = test_CURRENTORIENT(boy)[2];
}

/* the stick's angle from a direction, which CorrectStickInfo and
   subBoyControl inline */
static inline int correctStick(void *dir, IosPadStick *stick) /* derived name */
{
    float buf[4];

    ConvertStickToAbsCoord(buf, stick);
    return _RotyGV(buf, dir);
}

/* the private-camera gate */
static __inline__ unsigned char boyPrivInsCamInScreen(void) /* derived name */
{
    float scr[4];

    if (PrivInsCamChk_Control() == 0) {
        return 0;
    }
    if (0.0f < IsPointIsInScreen(scr, test_CURRENTROOT(boyGObj))) {
        return 1;
    }
    return 0;
}

/* the stick snap the boy's walk control applies when the camera-relative
   wish and the stick agree closely enough */
static __inline__ int snapStickToCamera(float *stick, float *wish, float *sabs,
                                        float *cam) /* derived name */
{
    int rc;
    int rs;
    int d;

    rc = _RotyGV(cam, wish);
    rs = _RotyGV(stick, sabs);
    if (sabs[0] == 0.0f && sabs[1] == 0.0f && sabs[2] == 0.0f) {
        return 0;
    }
    if (!((rc < 0 ? -rc : rc) < 20)) {
        return 0;
    }
    if ((rs < 0 ? -rs : rs) < 46) {
        return 0;
    }
    d = ((rs < 0 ? -rs : rs) > 90) ? 2 : 4;
    if (rs < 0) {
        d = -d;
    }
    if ((rs < 0 ? -rs : rs) < (d < 0 ? -d : d)) {
        d = rs;
    }
    stick[0] = sabs[0];
    stick[1] = sabs[1];
    stick[2] = sabs[2];
    _ApplyRyGV(stick, (float)d * 3.1415927f / 180.0f);
    return d;
}

int test_rope_slope = 0;

float add_rope_val = 0.0f;

void subBoyControl(GObj *volatile self)
{
    float stick[4];
    float wish[4];
    float sabs[4];
    float dir[4];
    int c0;
    int c1;
    int c2;
    Act *s = GOBJ_ACT(self);
    void *g;
    int n;
    int sw;
    int slow;
    float d;
    float dist;
    int dbg = 0; /* local debug switch, see the test after the stick loop */

    memset(stick, 0, 16);
    c0 = 0;
    c1 = 0;
    c2 = 0;
    memset(wish, 0, 16);
    n = 0;
    iosPadConnect(&s->pad, 0, 0, &iosPadConfCustom);
    boyPad = &s->pad;
    E3_StageStartBoy((void *)self);
    layoutActPushStartNew = 0;
    while (1) {
        if (FixViewInGameCameraFlag) {
            n = 3;
        }
        if (InsertCameraWorkingFlag) {
            n = 3;
        }
        if ((int)(s->flags20.ll >> 31) & 1) {
            n = 3;
        }
        sw = 0;
        if (n) {
            n--;
            sw = 1;
        }
        for (;;) {
            if (((int)(s->flags18.ll >> 48) & 1) == 0) {
                goto noStick;
            }
            s->flags18.ll &= ~0x800000000;
            if (scpBoyControlReadDisable == 0 && GOBJ_WORK(self)->pinchFrames == 0 &&
                (PrivInsCamChk() == 0 || boyPrivInsCamInScreen())) {
                iosPadRead(&s->pad);
                if (GOBJ_WORK(self)->ditchTimer != 0) {
                    s->pad.now &= ~8;
                }
                s->flags18.ll |= 0x800000000;
                if ((int)(s->pad.dev->flags >> 16) & 1) {
                    layoutActPushStartNew = 1;
                } else {
                    layoutActPushStartNew = 0;
                }
                /* the option word's low byte */
                iosPadGetStick(&s->pad, &s->stick, 0, 2, 2, (unsigned char)debug_stick_simulate);
                if (debug_stick_input) {
                    if (debug_font_flag & 1) {
                        debug_Printf(10, 170, 0x0FFFFFFF, "L = %f\n", fptodp(s->stick.mag));
                    }
                }
                ableBoyControl = 1;
            } else {
                s->pad.rel = 0;
                s->stick.x = s->stick.y = 127;
                s->pad.trg = 0;
                s->pad.now = 0;
                s->stick.mag = 0.0f;
                ableBoyControl = 0;
            }
            _GetMotionDirection(dir, (void *)self);
            correctStick(dir, &s->stick);
            if (s->pad.trg & 1) {
                BridgeBox();
            }
            g = isysGObjSearchFromObjLayoutID(2);
            if (s->wayMode == 1) {
                float tgt[4];
                float pos[4];

                GetRootPosition(tgt, g);
                GetRootPosition(pos, (void *)self);
                if (GetWay_begin(tgt, &s->way, pos) == 0) {
                    s->wayMode = 0;
                } else {
                    s->wayMode = 2;
                }
                break;
            }
            if (s->wayMode == 2) {
                switch (s->way.reached) {
                case 0: {
                    float root[4];

                    GetRootPosition(root, (void *)self);
                    GetWay_next(&s->way, root);
                    sceVu0CopyVector(stick, s->way.nrm);
                    s->stick.mag = 1.0f;
                    break;
                }
                case 1: {
                    float p0[4];
                    float p1[4];
                    ClipWork work;

                    GetRootPosition(p0, (void *)self);
                    GetRootPosition(p1, g);
                    work.radius = 10.0f;
                    sceVu0CopyVector(work.pt[0], p0);
                    sceVu0CopyVector(work.pt[1], p1);
                    ClipWall(&work);
                    if (work.wall.elem != 0) {
                        s->wayMode = 1;
                    }
                    dist = fzMagnitude2fv(p1, p0);
                    p1[0] = p1[0] - p0[0];
                    p1[1] = 0.0f;
                    p1[2] = p1[2] - p0[2];
                    sceVu0Normalize(stick, p1);
                    if (dist < 120.0f) {
                        ACTSendMailCorrect(self, 0xC7);
                        s->wayMode = 0;
                    } else if (dist < 850.0f) {
                        s->stick.mag = (dist - 100.0f) / 750.0f;
                    } else {
                        s->stick.mag = 1.0f;
                    }
                    break;
                }
                }
                break;
            }
            if (0.1f < s->stick.mag) {
                float cam[4];

                sabs[0] = stick[0];
                sabs[1] = stick[1];
                sabs[2] = stick[2];
                ConvertStickToAbsCoord(stick, &s->stick);
                cam[0] = (float)(s->stick.x - 128);
                cam[1] = 0.0f;
                cam[2] = (float)(s->stick.y - 128);
                if (sw == 0) {
                    snapStickToCamera(stick, wish, sabs, cam);
                }
                wish[0] = cam[0];
                wish[1] = cam[1];
                wish[2] = cam[2];
                {
                    float ori[4];

                    GetRootMotionOrient(ori, (void *)self);
                    s->stick.angle = _RotyGV(stick, ori);
                }
            } else {
                s->stick.angle = 0;
                s->stick.mag = 0.0f;
            }
            if ((void *)self == CurrentTargetGObj) {
                break;
            }
            _ACTWait(1);
        }
        /* a local debug switch, off: the cursor_control idiom of way_tool.c,
           moving the current target object */
        if (dbg) {
            if ((void *)self == CurrentTargetGObj && (s->pad.trg & 1)) {
                if (s->actMode != 1) {
                    ACTSendMailCorrect(self, 258);
                }
                ACTDebugMove(self, 1);
            }
        }
        s->dir[0] = stick[0];
        s->dir[1] = stick[1];
        s->dir[2] = stick[2];
    noStick:
        GOBJ_WORK(self)->stickMag = s->stick.mag;
        {
            static int slowWalkTimer = 0; /* derived name */

            slow = 0;
            if (s->actMode == 1) {
                if (GOBJ_SUB(self)->ctrl.motion == 0 || GOBJ_SUB(self)->ctrl.motion == 1) {
                    if (0.5f < s->stick.mag) {
                        slowWalkTimer = (60 - systemStatus[0] * 10) / systemStatus[1] / 5;
                    }
                }
            }
            if (0 < slowWalkTimer) {
                slowWalkTimer--;
                slow = 1;
            }
        }
        if (slow && 0.5f < s->stick.mag) {
            s->stick.mag = 0.5f;
        }
        c0++;
        if (0.1f < s->stick.mag) {
            c0 = 0;
        }
        if (0.1f < s->stick.mag && (s->stick.mag < 0.99f || (s->pad.now & 0x20))) {
            c1++;
        } else {
            c1 = 0;
        }
        if (0.1f < s->stick.mag &&
            !(0.1f < s->stick.mag && (s->stick.mag < 0.99f || (s->pad.now & 0x20)))) {
            c2++;
        } else {
            c2 = 0;
        }
        _ACTCommonMailTest(self, c0, c1, c2);
        switch (s->actMode) {
        case 1:
            ACTSendMailCorrect(self, 0xC7);
            break;
        case 2:
            ACTSendMailCorrect(self, 0xB5);
            break;
        case 3:
            ACTSendMailCorrect(self, 0xBA);
            break;
        case 41:
            if (0.1f < s->stick.mag && !((unsigned int)(s->stick.angle + 134) < 269)) {
                ACTSendMailCorrect(self, 0x149);
            }
            break;
        case 29:
            if (s->pad.trg & 0x40) {
                if (100.0f < GetDifferenceFromLowerField(self, 44)) {
                    ACTSendMailCorrect(self, 0x127);
                } else {
                    ACTSendMailCorrect(self, 0xE2);
                }
            }
            if (s->pad.now & 0x10) {
                ACTSendMailCorrect(self, 0xC7);
            }
            if (0.1f < s->stick.mag && (unsigned int)(s->stick.angle - 46) < 89) {
                ACTSendMailCorrect(self, 0x14E);
            }
            if (0.1f < s->stick.mag && !(s->stick.angle < -134) && s->stick.angle < -45) {
                ACTSendMailCorrect(self, 0x14F);
            }
            ACTSendMailCorrect(self, 0x150);
            break;
        case 26:
            if (0.1f < s->stick.mag && !((unsigned int)(s->stick.angle + 134) < 269)) {
                sceVu0ScaleVector(GOBJ_WORK(self)->fallDir,
                                  ICO_RAWP(char *, GOBJ_ACT(self)->work, 0x8D0,
                                           (char *)GOBJ_WORK(self)->cliffOrient),
                                  -1.0f);
                ACTSendMailCorrect(self, 0x139);
            }
            break;
        case 27:
            if (0.1f < s->stick.mag &&
                _AbsRotyGV(stick, (char *)GOBJ_WORK(self)->hangOrient) >= 136 &&
                GetMotionFrameFlag1((void *)self)) {
                ACTSendMailCorrect(self, 0x131);
            }
            if ((s->pad.now & 0x10) && GetMotionFrameFlag1((void *)self)) {
                ACTSendMailCorrect(self, 0x131);
            }
            if (0.1f < s->stick.mag && !((unsigned int)(s->stick.angle + 134) < 269)) {
                ACTSendMailCorrect(self, 0x130);
            }
            if (s->pad.trg & 0x40) {
                ACTSendMailCorrect(self, 0xE2);
            }
            if (s->pad.now & 0x10) {
                ACTSendMailCorrect(self, 0xC7);
            }
            if (0.1f < s->stick.mag && (unsigned int)(s->stick.angle - 46) < 89) {
                ACTSendMailCorrect(self, 0x14E);
            }
            if (0.1f < s->stick.mag && !(s->stick.angle < -134) && s->stick.angle < -45) {
                ACTSendMailCorrect(self, 0x14F);
            }
            ACTSendMailCorrect(self, 0x127);
            break;
        case 28:
            if (s->pad.trg & 0x40) {
                ACTSendMailCorrect(self, 0xE2);
            }
            if (0.1f < s->stick.mag && (unsigned int)(s->stick.angle - 46) < 89) {
                ACTSendMailCorrect(self, 0x14E);
            }
            if (0.1f < s->stick.mag && !(s->stick.angle < -134) && s->stick.angle < -45) {
                ACTSendMailCorrect(self, 0x14F);
            }
            ACTSendMailCorrect(self, 0x150);
            break;
        case 30:
            if (s->pad.trg & 0x40) {
                ACTSendMailCorrect(self, 0xE2);
            } else if (s->pad.trg & 0x10) {
                ACTSendMailCorrect(self, 0xC7);
            }
            if (0.1f < s->stick.mag && (unsigned int)(s->stick.angle - 46) < 89) {
                ACTSendMailCorrect(self, 0x14E);
            }
            if (0.1f < s->stick.mag && !(s->stick.angle < -134) && s->stick.angle < -45) {
                ACTSendMailCorrect(self, 0x14F);
            }
            ACTSendMailCorrect(self, 0x150);
            break;
        case 31:
            if (0.1f < s->stick.mag && (unsigned int)(s->stick.angle - 46) < 89) {
                ACTSendMailCorrect(self, 0x14E);
            }
            if (0.1f < s->stick.mag && !(s->stick.angle < -134) && s->stick.angle < -45) {
                ACTSendMailCorrect(self, 0x14F);
            }
            ACTSendMailCorrect(self, 0x150);
            break;
        case 33:
            if (s->pad.trg & 0x40) {
                ACTSendMailCorrect(self, 0xE2);
            }
            break;
        case 40:
            if ((s->pad.trg & 0x10) || s->stick.y - 128 < -100) {
                ACTSendMailCorrect(self, 0x12F);
            }
            if (s->pad.trg & 0x40) {
                ACTSendMailCorrect(self, 0xE2);
            }
            break;
        case 34:
            if (s->pad.trg & 0x40) {
                ACTSendMailCorrect(self, 0xE2);
            } else if (s->pad.now & 0x10) {
                ACTSendMailCorrect(self, 0x12E);
            }
            if (0.1f < s->stick.mag && (unsigned int)(s->stick.angle - 46) < 89) {
                ACTSendMailCorrect(self, 0x14E);
            }
            if (0.1f < s->stick.mag && !(s->stick.angle < -134) && s->stick.angle < -45) {
                ACTSendMailCorrect(self, 0x14F);
            }
            ACTSendMailCorrect(self, 0x150);
            break;
        case 35:
            if (s->pad.trg & 0x40) {
                ACTSendMailCorrect(self, 0xE2);
            }
            if (s->pad.trg & 0x10) {
                ACTSendMailCorrect(self, 0xBD);
            }
            if (0.1f < s->stick.mag && (unsigned int)(s->stick.angle - 46) < 89) {
                ACTSendMailCorrect(self, 0x14E);
            }
            if (0.1f < s->stick.mag && !(s->stick.angle < -134) && s->stick.angle < -45) {
                ACTSendMailCorrect(self, 0x14F);
            }
            break;
        case 52: {
            int near = 1;
            int front = 1;
            float gpos[4];
            float tpos[4];
            float ori[4];
            float vec[4];
            void *obj = s->env.pullParent;

            if (obj != 0 && girlGObj != 0) {
                gpos[0] = test_CURRENTROOT(girlGObj)[0];
                gpos[1] = test_CURRENTROOT(girlGObj)[1];
                gpos[2] = test_CURRENTROOT(girlGObj)[2];
                GetRootPosition(tpos, obj);
                ori[0] = test_CURRENTORIENT((void *)self)[0];
                ori[1] = test_CURRENTORIENT((void *)self)[1];
                ori[2] = test_CURRENTORIENT((void *)self)[2];
                _OrientXZGV(vec, gpos, tpos);
                if (_DistSqGV(tpos, gpos) < 250000.0f &&
                    CheckFloorAttribute(girlGObj, 0xA000000) != 0) {
                    if (0.0f < sceVu0InnerProduct(ori, vec)) {
                        near = 0;
                    } else {
                        front = 0;
                    }
                }
            }
            if (s->pad.now & 0x20) {
                if (near && 0.1f < s->stick.mag && (unsigned int)(s->stick.angle + 90) < 181) {
                    ACTSendMailCorrect(self, 0x81);
                    break;
                }
                if (front && 0.1f < s->stick.mag && !((unsigned int)(s->stick.angle + 89) < 179)) {
                    ACTSendMailCorrect(self, 0x80);
                    break;
                }
                ACTSendMailCorrect(self, 0x150);
                break;
            }
            ACTSendMailCorrect(self, 0x150);
            ACTSendMailCorrect(self, 0xC7);
            break;
        }
        case 53:
            if ((60 - systemStatus[0] * 10) / systemStatus[1] * 90 / 60 > s->modeFrame) {
                ACTSendMailCorrect(self, 0x86);
                break;
            }
            ACTSendMailCorrect(self, 0x150);
            ACTSendMailCorrect(self, 0xC7);
            break;
        case 54:
            if (s->pad.now & 0x20) {
                if (0.1f < s->stick.mag) {
                    ACTSendMailCorrect(self, 0x86);
                    if (s->pad.now & 8) {
                        ACTSendMailCorrect(self, 0x42);
                    }
                } else {
                    ACTSendMailCorrect(self, 0x150);
                }
            } else {
                ACTSendMailCorrect(self, 0xC7);
            }
            break;
        case 32:
        case 38: {
            /* the actor, read at the head of this arm; the DEBUG build's report
               reads it */
            GObj *obj = self;

            if (((int)(*(unsigned long long *)&GOBJ_ACT(self)->enemy->word298 >> 1) & 1) &&
                !(s->stick.y - 128 < 101)) {
                ACTSendMailCorrect(self, 0x14B);
            } else if ((GOBJ_ACT(self)->enemy->word298 & 1) && s->stick.y - 128 < -100) {
                ACTSendMailCorrect(self, 0x14A);
            } else {
                ACTSendMailCorrect(self, 0x150);
            }
            if ((optionControlType == 1 ? s->pad.trg : s->pad.now) & 8) {
                ACTSendMailCorrect(self, 0x42);
            }
#ifdef DEBUG
            scePrintf("boy %08x stick %d\n", obj, s->stick.y - 128);
#endif
            break;
        }
        case 43:
            if (0.95f < s->stick.mag) {
                ACTSendMailCorrect(self, 0xDE);
                break;
            }
            ACTSendMailCorrect(self, 0x150);
            break;
        case 44:
            if (0.95f < s->stick.mag) {
                ACTSendMailCorrect(self, 0xDD);
                break;
            }
            ACTSendMailCorrect(self, 0x150);
            break;
        case 49:
            if (s->pad.now & 0x20) {
                switch (s->pushDir) {
                case 1:
                    if (0.1f < s->stick.mag && (unsigned int)(s->stick.angle + 90) < 181) {
                        ACTSendMailCorrect(self, 0x14C);
                    } else {
                        ACTSendMailCorrect(self, 0x150);
                    }
                    break;
                case -1:
                    if (0.1f < s->stick.mag && !((unsigned int)(s->stick.angle + 89) < 179)) {
                        ACTSendMailCorrect(self, 0x14D);
                    } else {
                        ACTSendMailCorrect(self, 0x150);
                    }
                    break;
                default:
                    if (0.1f < s->stick.mag && (unsigned int)(s->stick.angle + 90) < 181) {
                        ACTSendMailCorrect(self, 0x14C);
                    } else if (0.1f < s->stick.mag &&
                               !((unsigned int)(s->stick.angle + 89) < 179)) {
                        ACTSendMailCorrect(self, 0x14D);
                    } else {
                        ACTSendMailCorrect(self, 0x150);
                    }
                    break;
                }
            } else {
                ACTSendMailCorrect(self, 0x150);
                ACTSendMailCorrect(self, 0xC7);
            }
            break;
        case 51:
            if (s->pad.now & 0x20) {
                if (0.1f < s->stick.mag && (unsigned int)(s->stick.angle + 90) < 181) {
                    ACTSendMailCorrect(self, 0x14C);
                } else if (0.1f < s->stick.mag && !((unsigned int)(s->stick.angle + 89) < 179)) {
                    ACTSendMailCorrect(self, 0x14D);
                } else {
                    ACTSendMailCorrect(self, 0x150);
                }
            } else {
                ACTSendMailCorrect(self, 0xC7);
            }
            break;
        case 118:
            if (s->pad.trg & 0x20) {
                ACTSendMailCorrect(self, 0xC8);
                break;
            }
            if (0.1f < s->stick.mag &&
                !(0.1f < s->stick.mag && (s->stick.mag < 0.99f || (s->pad.now & 0x20)))) {
                ACTSendMailCorrect(self, 0xBA);
                break;
            }
            if (0.1f < s->stick.mag && (s->stick.mag < 0.99f || (s->pad.now & 0x20))) {
                ACTSendMailCorrect(self, 0xB5);
                break;
            }
            ACTSendMailCorrect(self, 0x150);
            break;
        case 58:
            switch (GetChainSlope()) {
            case 0:
                ACTSendMailCorrect(self, 0xA3);
                if (GOBJ_SUB(self)->ctrl.motion == 137 && GOBJ_SUB(self)->ctrl.animFrame < 50.0f) {
                    ACTSendMailCorrect(self, 0xA4);
                }
                break;
            case 1:
                ACTSendMailCorrect(self, 0x94);
                break;
            case 2:
                ACTSendMailCorrect(self, 0x95);
                break;
            case 3:
                ACTSendMailCorrect(self, 0x96);
                break;
            case 4:
                ACTSendMailCorrect(self, 0x97);
                break;
            }
            if (GOBJ_SUB(self)->ctrl.motion == 135) {
                d = 1.0f;
            } else if (s->pad.now & 0x20) {
                d = 1.0f;
            } else {
                d = 0.0f;
            }
            SaveBoyOrientForScript();
            add_rope_val = d;
            if (s->pad.now & 0x20) {
                IncreasePdlChain(s->chain);
            } else {
                DecreasePdlChain(s->chain);
            }
            if (s->pad.trg & 0x10) {
                ACTSendMailCorrect(self, 0xBE);
                ACTSendMailCorrect(self, 0xC4);
            }
            if (s->pad.trg & 0x40) {
                ACTSendMailCorrect(self, 0x13C);
            }
            break;
        case 60:
            if (!(s->stick.y - 128 < 101)) {
                ACTSendMailCorrect(self, 0x13C);
            }
            if (s->stick.y - 128 < -100) {
                ACTSendMailCorrect(self, 0x9E);
            }
            if (s->pad.now & 0x20) {
                ACTSendMailCorrect(self, 0x9F);
            }
            if (s->pad.trg & 0x40) {
                ACTSendMailCorrect(self, 0x13C);
            }
            break;
        case 20:
        case 21:
            if (girlGObj != 0) {
                iosOmSendMail(girlGObj, 0x3E, isysCurrentGObj);
            }
            break;
        case 109:
            if (s->pad.now & 8) {
                break;
            }
            ACTSendMailCorrect(self, 0xC7);
            break;
        case 105:
            if (0.1f < s->stick.mag && (unsigned int)(s->stick.angle + 45) < 91) {
                ACTSendMailCorrect(self, 0x17C);
            }
            if (0.1f < s->stick.mag && !((unsigned int)(s->stick.angle + 134) < 269)) {
                ACTSendMailCorrect(self, 0xE2);
            }
            break;
        case 45:
            if (0.1f < s->stick.mag && (unsigned int)(s->stick.angle + 45) < 91) {
                ACTSendMailCorrect(self, 0x75);
                ACTSendMailCorrect(self, 0x74);
            }
            if (s->pad.now & 8) {
                ACTSendMailCorrect(self, 0x74);
                brainAddLevelGirl(10.0f);
            }
            if (s->modeFrame == 0 && ((int)(s->flags18.ll >> 41) & 1)) {
                brainAddLevelGirl(1000.0f);
            }
            break;
        case 23:
            if (0.1f < s->stick.mag && (unsigned int)(s->stick.angle + 45) < 91) {
                ACTSendMailCorrect(self, 0x14C);
                break;
            }
            if (0.1f < s->stick.mag && !((unsigned int)(s->stick.angle + 134) < 269)) {
                ACTSendMailCorrect(self, 0x14D);
                break;
            }
            if (0.1f < s->stick.mag && (unsigned int)(s->stick.angle - 46) < 89) {
                ACTSendMailCorrect(self, 0x14E);
                break;
            }
            if (0.1f < s->stick.mag && !(s->stick.angle < -134) && s->stick.angle < -45) {
                ACTSendMailCorrect(self, 0x14F);
            }
            break;
        case 94:
            if (GOBJ_ACT(self)->enemy->floorAttrOff != 0) {
                ACTSendMailCorrect(self, 0x16F);
                break;
            }
            ACTSendMailCorrect(self, 0x150);
            break;
        }
        _ACTWait(1);
    }
}

typedef struct {                      /* field names derived */
    int boyID;                        /* 0x00 */
    int girlID;                       /* 0x04 */
    unsigned long long layoutID : 32; /* 0x08 */
    /* the one-bit flags are declared no wider than short; torch is a
       short */
    unsigned char bit32 : 1;
    unsigned char fire : 1;
    unsigned short torch : 1;
    unsigned char escort : 1;
    char pad10[16];          /* 0x10 */
    void *weapon;            /* 0x20 */
    void *nextWeapon;        /* 0x24 */
    char pad28[0x30 - 0x28]; /* 0x28 */
    float f30;               /* 0x30 */
    float f34;
    float f38;
    char pad3C[4];
    float f40; /* 0x40 */
    float f44;
    float f48;
    char pad4C[4];
    CharPos f50; /* 0x50 */
} BoyInfo;       /* derived name */

#define BOYINFO (*(BoyInfo *)boyInfo) /* derived name */
/* BoyInfo's +0x50 record as the ef-stage return reads it: a flag byte and the
   camera target id at +4 (BoyInfoUpdate_StageChange copies it whole as f50). */
#define BOYEFSTAGE ((unsigned char *)&BOYINFO.f50) /* derived name */

/* PC port: Boy_Init resets the record by copying boyInfoDefault (a BoyWork)
   over the buffer, and the raw word stores (((int *)boyInfo)[4] = -1, the
   flag bits of boyInfo[1]) only keep their EE places while the pointers
   start at 0x20; the template must cover the whole host record
   (tools/template_audit.py) */
_Static_assert(sizeof(BoyWork) == sizeof(BoyInfo) && sizeof(boyInfo) >= sizeof(BoyInfo) &&
                   __builtin_offsetof(BoyInfo, weapon) == 0x20,
               "BoyWork does not cover BoyInfo on the host");

static void InitSwapWeapon(void *self)
{
    Act *sub = GOBJ_ACT(self);
    GObj *info;
    char *p;
    GenGeo *row;

    if (BOYINFO.boyID != 0) {
        BOYINFO.weapon = isysGObjSearchFromObjLayoutID(BOYINFO.boyID);
    } else {
        BOYINFO.weapon = 0;
    }
    info = sub->env.swapWeapon;
    BOYINFO.nextWeapon = info;
    p = (char *)gamesysObjInfoGet(info->kind, info->labelId);
    if (p != 0) {
        BOYINFO.f30 = *(float *)(p + 0x10);
        BOYINFO.f34 = *(float *)(p + 0x14);
        BOYINFO.f38 = *(float *)(p + 0x18);
        BOYINFO.f40 = *(float *)(p + 0x20);
        BOYINFO.f44 = *(float *)(p + 0x24);
        BOYINFO.f48 = *(float *)(p + 0x28);
    } else {
        row = &objLayout[info->labelId];
        BOYINFO.f30 = -row->pos[0];
        BOYINFO.f34 = -row->pos[1];
        BOYINFO.f38 = -row->pos[2];
        BOYINFO.f40 = row->rot[0] * 3.1415927f / 180.0f;
        BOYINFO.f44 = row->rot[1] * 3.1415927f / 180.0f;
        BOYINFO.f48 = row->rot[2] * 3.1415927f / 180.0f;
    }
}

static void PutWeapon(void)
{
    char *p = (char *)boyInfo;

    if (BOYINFO.weapon != 0) {
        InitMotionGeoInfo(&GOBJ_SUB(BOYINFO.weapon)->root, BOYINFO.f30, BOYINFO.f34, BOYINFO.f38,
                          -BOYINFO.f40, -BOYINFO.f44, -BOYINFO.f48);
        if (CheckWeaponKind(BOYINFO.weapon) == 9) {
            SetWeaponOffsetMode(BOYINFO.weapon, 1);
        }
        UpdateRootMatrix(BOYINFO.weapon);
    }
}

/* a static helper inlined into SetBoyWeaponGObj and afterBoyTakeWeapon */
static inline int SwapBoyWeapon(void *oldW, void *newW, void *boy) /* derived name */
{
    Act *sub = GOBJ_ACT(boy);

    if (oldW == newW) {
        return 0;
    }
    if (newW == 0) {
        debug_assert(__FILE__, 3007);
        __assert(__FILE__, 3007, "next");
        return 0;
    }
    PickupWeapon(newW, boy, 0x16);
    ((int *)boyInfo)[0] = ICO_RAW(int, newW, 0x8, ((GObj *)newW)->labelId);
    sub->weapon = newW;
    SetWeaponOffsetMode(newW, 0);

    if (oldW == 0) {
        return 1;
    }
    ReleaseWeapon(oldW);
    PutWeapon();
    gamesysObjInfoPosSetStage(oldW, 0, 0, stage_no);
    debug_StdPrintfDummy("%d -> %d\n", ICO_RAW(int, oldW, 0x8, ((GObj *)oldW)->labelId),
                         ICO_RAW(int, newW, 0x8, ((GObj *)newW)->labelId));
    return 1;
}

/* returns float * here, int * in camera-root.h */
extern float *GetCurrentCameraSet2(void);

static void OtherStageGirlPinchCamera_After(float t)
{
    float buf[4];

    pinchBoyPos[0] = test_CURRENTROOT(boyGObj)[0];
    pinchBoyPos[1] = test_CURRENTROOT(boyGObj)[1];
    pinchBoyPos[2] = test_CURRENTROOT(boyGObj)[2];
    if (_ACTGame_GetParamF(0xE) * (float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f <=
        t) {
        pinchBoyPosLate[0] = test_CURRENTROOT(boyGObj)[0];
        pinchBoyPosLate[1] = test_CURRENTROOT(boyGObj)[1];
        pinchBoyPosLate[2] = test_CURRENTROOT(boyGObj)[2];
        GetOtherStageGirlOrient(buf, GetCurrentCameraSet2());
        sceVu0ScaleVector(buf, buf, 500.0f);
        sceVu0AddVector(pinchCameraPos, GetCurrentCameraSet2(), buf);
    } else {
        PrivInsCamSet(test_CURRENTROOT(boyGObj), pinchCameraPos, boyGObj,
                      (60 - systemStatus[0] * 10) / systemStatus[1] * 100 / 60,
                      (60 - systemStatus[0] * 10) / systemStatus[1] * 45 / 60, 0.05f, 0.25f, 0);
        GOBJ_WORK(boyGObj)->pinchFrames = 0;
    }
}

void ACTDispLwsBoyStonize_InQueenStage(void *self)
{
    BoyBgaManager(self, 0x1E0, (char *)&GOBJ_ACT(self)->enemy->stonePair);
    BoyBgaManager(self, 0x1E1, (char *)&GOBJ_ACT(self)->enemy->word2A4);
    BoyBgaManager(self, 0x1E5, (char *)&GOBJ_ACT(self)->enemy->word2B0);
    BoyBgaManager(self, 0x1E5, (char *)&GOBJ_ACT(self)->enemy->stoneHitWeapon);
    BoyBgaManager(self, 0x1E6, (char *)&GOBJ_ACT(self)->enemy->stoneHitNoWeapon);
}

static int characterPacket[8]; /* derived name */

/* PC port: MakeCharacterPacket and Boy_Init reset it from a BoyKidnapWork */
_Static_assert(sizeof(BoyKidnapWork) == sizeof(characterPacket),
               "BoyKidnapWork is not the character packet");

static PrivInsCam privInsCam; /* derived name */

/* as in camera-root.h, which this TU does not include (GetCurrentCameraSet2 differs) */
extern void InsertCamera_SetDetail(float *pos, float *tgt, int frames, int cutType, int zoom,
                                   int cutBack, float blend);

static void PrivInsCamProcess(void)
{
    float p[4];

    switch (privInsCam.on) {
    case 1:
        privInsCam.cur[0] = privInsCam.pos[0];
        privInsCam.cur[1] = privInsCam.pos[1];
        privInsCam.cur[2] = privInsCam.pos[2];
        privInsCam.cnt = privInsCam.inFrames;
        privInsCam.on = 2;
        break;
    case 2:
        _InterGV(privInsCam.cur, privInsCam.tgt, privInsCam.cur, 1.0f, privInsCam.inRate);
        InsertCamera_SetDetail(GetCurrentCameraSet2(), privInsCam.cur, 5, 1, 2, 0,
                               privInsCam.blend);
        if (--privInsCam.cnt <= 0) {
            privInsCam.on = 3;
        }
        break;
    case 3:
        privInsCam.tgt[0] = privInsCam.cur[0];
        privInsCam.tgt[1] = privInsCam.cur[1];
        privInsCam.tgt[2] = privInsCam.cur[2];
        privInsCam.cnt = privInsCam.outFrames;
        privInsCam.on = 4;
        break;
    case 4:
        if (privInsCam.track != 0) {
            p[0] = test_CURRENTROOT(privInsCam.track)[0];
            p[1] = test_CURRENTROOT(privInsCam.track)[1];
            p[2] = test_CURRENTROOT(privInsCam.track)[2];
        } else {
            p[0] = privInsCam.pos[0];
            p[1] = privInsCam.pos[1];
            p[2] = privInsCam.pos[2];
        }
        _InterGV(privInsCam.cur, p, privInsCam.tgt, (float)privInsCam.cnt,
                 (float)(privInsCam.outFrames - privInsCam.cnt));
        InsertCamera_SetDetail(GetCurrentCameraSet2(), privInsCam.cur, 2, 1, 0, 0,
                               privInsCam.blend);
        if (--privInsCam.cnt <= 0) {
            privInsCam.on = 0;
        }
        break;
    }
}

/* as in camera-root.h, which this TU does not include (GetCurrentCameraSet2 differs) */
extern void Camctrl_SetTarget(GObj *gobj, GObj *subGObj, int pri);

/* the object kinds the proximity scan below walks, terminated by -1 */
typedef struct { /* field names derived */
    int id[4];
} ObjKindList; /* derived name */

static const ObjKindList collisionKinds = {{4, 47, 62, -1}}; /* derived name */

/* reset the private insert camera to its initial value; only
   subBoyCollision calls it */
static inline void PrivInsCamInit(void) /* derived name */
{
    privInsCam = privInsCamDefault;
}

/* the nearest object of a kind in front of the actor, which ACTSearchGObj,
   subBoyCollision and actBoyAttack inline */
static inline void searchGObj(void *self, int kind, int maxDeg, ICO_WORD *out_id, float *out_vec,
                              float thresh) /* derived name */
{
    float buf[4];
    void *node;
    int best;

    node = isysGObjSearchFromObjKindID_begin(kind);
    best = maxDeg;
    *out_id = 0;
    for (; node != 0; node = isysGObjSearchFromObjKindID_next(node)) {
        if (((GObj *)node)->active != 0) {
            float *r1 = test_CURRENTROOT(self);
            if (_DistGV(r1, test_CURRENTROOT(node)) < thresh) {
                int sign;
                int dist;
                float *r4 = test_CURRENTROOT(node);
                sceVu0SubVector(buf, r4, test_CURRENTROOT(self));
                sign = (int)_RotyGV(buf, test_CURRENTORIENT(self));
                if (sign < 0) {
                    dist = -(int)_RotyGV(buf, test_CURRENTORIENT(self));
                } else {
                    dist = (int)_RotyGV(buf, test_CURRENTORIENT(self));
                }
                if (dist < best) {
                    best = dist;
                    out_vec[0] = buf[0];
                    out_vec[1] = buf[1];
                    out_vec[2] = buf[2];
                    *out_id = (ICO_WORD)node;
                }
            }
        }
    }
}

/* the DEBUG build's report of subBoyCollision's camera state, built only
   under DEBUG */
static __inline__ void boyCamDebugDisp(int camOn, int looking) /* derived name */
{
#ifdef DEBUG
    scePrintf("boy camera on %d looking %d\n", camOn, looking);
#endif
}

static void *searchWeapon(void) /* derived name */
{
    void *g;

    for (g = isysGObjSearchFromObjKindID_begin(0xE); g != 0;
         g = isysGObjSearchFromObjKindID_next(g)) {
        if (CheckWeaponKind(g) == 5) {
            return g;
        }
    }
    return 0;
}

void subBoyCollision(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    int camOn;
    int looking;
    int hang;
    int hangBit;
    int hold;
    int dbg = 0; /* local debug switch, see the test after the helper's SetRootPosition */

    PrivInsCamInit();

    while (sub->motReq == 0) {
        _ACTWait(1);
    }
    if (200.0f < GetDifferenceFromLowerField(self, 0x2C)) {
        ACTSendMailCorrect(self, 0x7);
    }
    while (1) {
        float vec[4];

        camOn = 0;
        if (0 < GOBJ_WORK(self)->pinchFrames) {
            GOBJ_WORK(self)->pinchFrames -= 1;
            _ACTCharStatus_Set((void *)self, 0x20, -1.0f, 0);
            OtherStageGirlPinchCamera_After((float)GOBJ_WORK(self)->pinchFrames);
        }
        PrivInsCamProcess();
        findChainInJump((void *)self);
        CheckCollisionAttr((void *)self);
        ACTGame_CommonLoop((void *)self);

        hangBit = ((int)(sub->flags18.ll >> 50) & 1);
        hang = 1;
        if (((int)(sub->flags20.ll >> 18) & 1) == 0) {
            hang = hangBit;
        }
        if (hang == 0) {
            if (sub->stick.mag != 0.0f &&
                CorrectOrient_RopeCliff(vec, (void *)self, sub->dir) != 0) {
                sub->dir[0] = vec[0];
                sub->dir[1] = vec[1];
                sub->dir[2] = vec[2];
            }
            if (0.1f < sub->stick.mag && sub->actMode != 0x73) {
                SetMotionDirectionSmooze(self, sub->dir,
                                         (float)(((void *)self == girlGObj && girlControlMode != 0)
                                                     ? CHAINROW(self)->girlDirFrames
                                                     : CHAINROW(self)->dirFrames));
            }
        }
        CommonAttackCenter((void *)self);
        ACTGame_SaveActorInformation(self);
        if (*(unsigned int *)&sub->actMode < 4 && sub->actMode != 0) {
            if (sub->pad.trg & 0x20) {
                ICO_WORD hit;

                searchGObj((void *)self, 0x13, 0x2D, &hit, vec, 100.0f);
            }
        }
        switch (sub->actMode) {
        case 0x1C:
            if (((int)(sub->wish1.ll >> 10) & 1) && ((int)(sub->wish3.ll >> 10) & 1)) {
                ACTSendMailCorrect(self, 0xC7);
            }
            break;
        case 0x20:
        case 0x26:
            if (sub->pad.trg & 0x40) {
                ACTSendMailCorrect(self, 0x13C);
            }
            break;
        case 0x39:
            SaveBoyOrientForScript();
            add_rope_val = 0.0f;
            if (sub->pad.trg & 0x10) {
                ACTSendMailCorrect(self, 0xC3);
            }
            if (sub->pad.trg & 0x40) {
                GOBJ_WORK(self)->ropeClimbHeight = test_CURRENTROOT((void *)sub->chain)[1] +
                                                   GetChainLength((void *)sub->chain) -
                                                   test_CURRENTROOT((void *)self)[1];
                ActSendMail_WithAdditionalData((void *)self, 0x13C, (void *)self,
                                               &GOBJ_WORK(self)->ropeClimbHeight);
            }
            if (sub->pad.now & 0x20) {
                ACTSendMailCorrect(self, 0xA2);
                ACTSendMailCorrect(self, 0xE3);
            } else {
                if (sub->stick.y - 0x80 < -100) {
                    ACTSendMailCorrect(self, 0x14A);
                } else if (100 < sub->stick.y - 0x80) {
                    ACTSendMailCorrect(self, 0x14B);
                    if (isBottomOfChain(sub->chain)) {
                        ACTSendMailCorrect(self, 0x9D);
                    }
                } else if (GOBJ_SUB(self)->ctrl.motion == 0x76) {
                    float bodyori[4];
                    int cor;
                    int ry;

                    GetCorrectOrientOfChain(vec, (void *)self);
                    SetMotionDirection((void *)self, vec);
                    if (GetChainDirCorrectVal((void *)sub->chain, &cor) != 0) {
                        if (sub->stick.x - 0x80 < -100) {
                            ACTSendMailCorrect(self, 0xA0);
                        }
                        if (100 < sub->stick.x - 0x80) {
                            ACTSendMailCorrect(self, 0xA1);
                        }
                    } else {
                        if (sub->stick.x - 0x80 < -100) {
                            ry = 5;
                        } else {
                            ry = 0;
                        }
                        if (100 < sub->stick.x - 0x80) {
                            ry = -5;
                        }
                        bodyori[0] = test_CURRENTORIENT((void *)self)[0];
                        bodyori[1] = test_CURRENTORIENT((void *)self)[1];
                        bodyori[2] = test_CURRENTORIENT((void *)self)[2];
                        _ApplyRyGV(bodyori, (float)ry * 3.1415927f / 180.0f);
                        SetMotionDirection((void *)self, bodyori);
                    }
                }
                ACTSendMailCorrect(self, 0x150);
            }
            break;
        case 0x42:
            if (((int)(sub->flags20.ll >> 11) & 1) == 0) {
                if (sub->stick.y - 0x80 < -100) {
                    ACTSendMailCorrect(self, 0x14A);
                }
                if (100 < sub->stick.y - 0x80) {
                    ACTSendMailCorrect(self, 0x14B);
                }
            }
            if (GOBJ_SUB(self)->ctrl.motion == 0x76) {
                if (sub->stick.x - 0x80 < -100) {
                    ACTSendMailCorrect(self, 0xA0);
                }
                if (100 < sub->stick.x - 0x80) {
                    ACTSendMailCorrect(self, 0xA1);
                }
            }
            ACTSendMailCorrect(self, 0x150);
            if (sub->pad.trg & 0x40) {
                ACTSendMailCorrect(self, 0x13C);
            }
            break;
        case 0x37:
            if (girlGObj != 0 && GOBJ_ACT(girlGObj)->curMot != 0x5E &&
                GOBJ_ACT(girlGObj)->curMot != 0x65 &&
                (60 - systemStatus[0] * 10) / systemStatus[1] < sub->modeFrame) {
                if (((int)(sub->wish0.ll >> 49) & 1) == 0 ||
                    ((int)(sub->wish2.ll >> 49) & 1) == 0) {
                    ACTSendMailCorrect(self, 0xF9);
                } else {
                    ACTSendMailCorrect(self, 0xFA);
                }
            }
            /* falls through into the next arm */
        case 0x44:
            if (((int)(sub->wish0.ll >> 48) & 1) && ((int)(sub->wish2.ll >> 48) & 1)) {
                if (girlGObj != 0) {
                    iosOmSendMail(girlGObj, 0x3E, isysCurrentGObj);
                }
                ACTSendMailCorrect(self, 0xFA);
            } else if (NotNeedBackHand() ||
                       (GOBJ_ACT(girlGObj)->intrMot != 0x5E &&
                        GOBJ_ACT(girlGObj)->intrMot != 0x65 &&
                        (60 - systemStatus[0] * 10) / systemStatus[1] * 2 < sub->modeFrame)) {
                if (((int)(sub->wish0.ll >> 49) & 1) && ((int)(sub->wish2.ll >> 49) & 1)) {
                    ACTSendMailCorrect(self, 0xFA);
                } else {
                    ACTSendMailCorrect(self, 0xF9);
                }
            }
            break;
        /* the table runs from 1, with this arm empty */
        case 0x1:
            break;
        }
        {
            int tgt2[4];
            int scr2[4];
            int work[4];
            int cam[4];
            int broot[4];
            int ofs[4];
            int cpos[4];

            looking = 0;
            if (((int)(sub->wish0.ll >> 46) & 1) == 0 || ((int)(sub->wish2.ll >> 46) & 1) == 0) {
                girlLookStarted = 0;
                girlLookInScreen = 0;
            }
            if (girlGObj != 0 && GOBJ_ACT(girlGObj)->actMode == 0x45) {
                sceVu0ScaleVector(vec, test_CURRENTORIENT((void *)self), 200.0f);
                vec[1] = 0.0f;
                sceVu0AddVector(vec, test_CURRENTROOT((void *)self), vec);
                _ACTLookTarget_Set((void *)self, 0, vec, 1, 1);
            }
            if (((int)(sub->wish0.ll >> 46) & 1) && ((int)(sub->wish2.ll >> 46) & 1)) {
                int see = 0;
                int onGirl = 0;

                if (girlLookStarted == 0) {
                    girlLookStarted = 1;
                    if (girlGObj != 0) {
                        girlLookInScreen =
                            (0.0f < IsPointIsInScreen(scr2, test_CURRENTROOT(girlGObj))) ? 1 : 0;
                    }
                }
                if (girlGObj != 0) {
                    ((float *)tgt2)[0] = test_CURRENTROOT(girlGObj)[0];
                    ((float *)tgt2)[1] = test_CURRENTROOT(girlGObj)[1];
                    ((float *)tgt2)[2] = test_CURRENTROOT(girlGObj)[2];
                    see = 1;
                    onGirl = 1;
                } else if ((stage_no == 0x56 || stage_no == 0x3 || stage_no == 0x2E) &&
                           ((int)(sub->flags20.ll >> 24) & 3)) {
                    see = 1;
                    onGirl = 0;
                    ScpCallCameraGetTarget((float *)tgt2);
                }
                if (see) {
                    float d = _DistGV(test_CURRENTROOT((void *)self), (float *)tgt2);

                    if (d < _ACTGame_GetParamF(3) && onGirl) {
                        _ACTParaStatus_Set((void *)self, 0x15);
                        _ACTCharStatus_Set((void *)self, 0x21, -1.0f, 0);
                    } else if (d < _ACTGame_GetParamF(4)) {
                        _ACTParaStatus_Set((void *)self, 0x14);
                        _ACTCharStatus_Set((void *)self, 0x23, -1.0f, 0);
                        _ACTParaStatus_Set((void *)self, 0x13);
                        _ACTCharStatus_Set((void *)self, 0x22, -1.0f, 0);
                    } else {
                        _ACTParaStatus_Set((void *)self, 0x13);
                        _ACTCharStatus_Set((void *)self, 0x22, -1.0f, 0);
                    }
                } else {
                    _ACTParaStatus_Set((void *)self, 0x13);
                    _ACTCharStatus_Set((void *)self, 0x22, -1.0f, 0);
                }
                hold = sub->pad.now & 0x8;
                looking = hold != 0;
            }
            if (girlGObj != 0 && (looking || ((int)(GOBJ_ACT(girlGObj)->flags20.ll >> 26) & 1))) {
                _ACTLookTarget_Set((void *)self, girlGObj, 0, 4, 2);
                if (girlLookInScreen == 0 && ((int)(sub->flags20.ll >> 23) & 1)) {
                    if (((int)(sub->flags20.ll >> 24) & 3) != 0) {
                        void *lo = isysGObjSearchFromObjLayoutID(0x4);

                        if (lo != 0) {
                            ScpCallCameraGetTarget((float *)work);
                            SetRootPosition(lo, work);
                            Camctrl_SetTarget(self, lo, 1);
                        }
                    } else {
                        Camctrl_SetTarget(self, girlGObj, 1);
                    }
                }
            }
            {
                int i;
                float near = 3.40282347e+38f; /* FLT_MAX */

                *(ObjKindList *)work = collisionKinds;
                for (i = 0; work[i] != -1; i++) {
                    void *g;

                    for (g = isysGObjSearchFromObjKindID_begin(work[i]); g != 0;
                         g = isysGObjSearchFromObjKindID_next(g)) {
                        if ((int)(GOBJ_ACT(g)->flags18.ll >> 32) & 1) {
                            float d = _DistGV(test_CURRENTROOT((void *)self), test_CURRENTROOT(g));

                            if (work[i] == 47) {
                                if (((GObj *)g)->active == 0) {
                                    continue;
                                }
                                d = 1.0f;
                            }
                            if (d < near) {
                                near = d;
                            }
                        }
                    }
                }
                if (ACTGame_NoWeapon((void *)self) == 0) {
                    if (near < 1000.0f) {
                        _ACTParaStatus_Set((void *)self, 0x2);
                    }
                    if (near < 300.0f) {
                        _ACTParaStatus_Set((void *)self, 0x3);
                    }
                }
                if (near < 1000.0f) {
                    _ACTCharStatus_Set((void *)self, 0x11, near, 0);
                }
            }
            if (_ACTParaStatus_Check((void *)self, 0x3) ||
                _ACTParaStatus_Check((void *)self, 0x2)) {
                if (_ACTParaStatus_Check((void *)self, 0x14)) {
                    _ACTParaStatus_Set((void *)self, 0x17);
                }
                if (_ACTParaStatus_Check((void *)self, 0x13)) {
                    _ACTParaStatus_Set((void *)self, 0x16);
                }
            }
            if (ACTGame_NoWeapon((void *)self)) {
                _ACTParaStatus_Set((void *)self, 0x4);
            }
            ACTParaStatus_Exec((void *)self);
            {
                void *w;

                if (girlGObj == 0 && (w = searchWeapon()) != 0) {
                    if ((sub->pad.now & 0x8) == 0) {
                        weaponLookInScreen = 0;
                        weaponLookStarted = 0;
                    } else {
                        int mode;
                        int ok;

                        if (weaponLookStarted == 0) {
                            weaponLookInScreen =
                                (0.0f < IsPointIsInScreen(work, test_CURRENTROOT(w))) ? 1 : 0;
                        }
                        weaponLookStarted = 1;
                        mode = (int)(sub->flags20.ll >> 24) & 3;
                        ok = mode == 0;
                        if (stage_no == 0x25 && mode == 2) {
                            ok = 1;
                        }
                        if (((int)(sub->flags20.ll >> 23) & 1) && ok) {
                            Camctrl_SetTarget(self, w, 1);
                            camOn = 1;
                            if (stage_no == 0x25) {
                                ((float *)cam)[0] = test_CURRENTROOT(w)[0];
                                ((float *)cam)[1] = test_CURRENTROOT(w)[1];
                                ((float *)cam)[2] = test_CURRENTROOT(w)[2];
                                ScpCallCameraSetTarget(-((float *)cam)[0], -((float *)cam)[1],
                                                       -((float *)cam)[2]);
                                sub->flags20.ll = (sub->flags20.ll & ~0x3000000) | 0x2000000;
                            }
                        }
                    }
                }
            }
            if (camOn == 0 && girlGObj == 0) {
                void *lo = isysGObjSearchFromObjLayoutID(0x4);

                /* In retail camOn and looking are dead from here on; the DEBUG
                   build's camera report at the end of this block reads
                   them. */
                if (lo == 0) {
                    camOn = 1;
                    looking = 0;
                }
                if ((sub->pad.now & 0x8) && lo != 0) {
                    ((float *)broot)[0] = test_CURRENTROOT(boyGObj)[0];
                    ((float *)broot)[1] = test_CURRENTROOT(boyGObj)[1];
                    ((float *)broot)[2] = test_CURRENTROOT(boyGObj)[2];
                    ((float *)cam)[0] = ((float *)GetCurrentCameraSet2())[0];
                    ((float *)cam)[1] = ((float *)GetCurrentCameraSet2())[1];
                    ((float *)cam)[2] = ((float *)GetCurrentCameraSet2())[2];
                    GetOtherStageGirlOrient((float *)ofs, (float *)cam);
                    sceVu0ScaleVector(ofs, ofs, _DistGV((float *)cam, (float *)broot));
                    sceVu0AddVector(work, cam, ofs);
                    SetRootPosition(lo, work);
                    /* a local debug switch, off (see dbg in the declarations): mark
                       the helper position just set */
                    if (dbg) {
                        debug_NMarker(work, 0xFF, 0, 0, 100.0f);
                    }
                    if ((int)(sub->flags20.ll >> 23) & 1) {
                        if (((int)(sub->flags20.ll >> 24) & 3) != 0) {
                            lo = isysGObjSearchFromObjLayoutID(0x4);
                            if (lo != 0) {
                                ScpCallCameraGetTarget((float *)cpos);
                                SetRootPosition(lo, cpos);
                                Camctrl_SetTarget(self, lo, 1);
                            }
                        } else {
                            Camctrl_SetTarget(self, lo, 1);
                        }
                    }
                }
                boyCamDebugDisp(camOn, looking);
            }
            ACTLookTargetSystem_Exec((void *)self);
            if (boyGObj != 0 && girlGObj != 0 && GOBJ_ACT(boyGObj)->actMode == 0x2D &&
                GOBJ_ACT(girlGObj)->actMode == GOBJ_ACT(boyGObj)->actMode) {
                if (0x3C < sitCount++) {
                    if (sitLayoutDone == 0) {
                        lt_switch_layout(0x1C);
                        sitLayoutDone = 1;
                    }
                }
            } else {
                sitLayoutDone = 0;
                sitCount = 0;
            }
            if ((_ACTCharStatus_Check((void *)self, 0x22) ||
                 _ACTCharStatus_Check((void *)self, 0x23)) &&
                girlGObj != 0) {
                iosOmSendMail(girlGObj, 0x3D, isysCurrentGObj);
            }
            sub->flags18.ll = (sub->flags18.ll & ~0x20000000000LL) |
                              ((unsigned long long)(ACTGame_FLAG_TETSUNAGI() & 1) << 41);
            if (((CHAINROW(self)->flags.word >> 13) & 1) && ACTGame_FLAG_TETSUNAGI() == 0) {
                ACTSendMailCorrect(self, 0x1AA);
            }
            if ((int)(sub->flags18.ll >> 38) & 1) {
                int life = GOBJ_WORK(self)->bit38Frames;

                if (life < 60) {
                    if (girlGObj != 0) {
                        iosOmSendMail(girlGObj, 0xB4, isysCurrentGObj);
                    }
                } else if (life < 70) {
                    brainAddLevelGirl(20.0f);
                } else {
                    brainAddLevelGirl(10.0f);
                }
            }
        }
        _ACTWait(1);
    }
}

inline void afterBoySwim(GObj *volatile self);

/* the record Act+0x680 points at, with the fields actBoyBelift and actBoySwim
   touch: the lift level and the lifted object, then the floating-box flag, the
   box GObj and the grip point, a four-float vector sceVu0ApplyMatrix takes
   whole (its w set to 1 before the apply) */
typedef struct { /* field names derived */
    char pad0[204];
    int liftLevel; /* 0xCC, the lift level actBoyBelift sets to 10 and clamps */
    char padD0[348];
    void *liftObj; /* 0x22C, the object the boy lifts (actBoyBelift stores the girl) */
    char pad230[144];
    int holdBox; /* 0x2C0, set while the boy holds a floating box */
    GObj *box;   /* 0x2C4 */
    char pad2C8[8];
    float grip[4]; /* 0x2D0 */
} BoyExt;          /* derived name */

#define BOY_EXT(o) (*(BoyExt **)(*(char **)((char *)(o) + 0x164) + 0x680)) /* derived name */
/* One BoyExt field f; the host reads the record as what it is, the actor's
   EnemyBattleWork, by that record's field hf (BoyExt's offsets are the EE's
   and the actor is not at GObj + 0x164 on a 64-bit host) */
#define BOY_EXT_F(o, f, hf) (GOBJ_ACT(o)->enemy->hf)

/* GObj's 0x15C slot read through a union (typedef.h: an int handle the
   engine casts to a pointer) */
typedef union { /* field names derived */
    char *sub;
} GObjSubSlot; /* derived name */

#define GOBJ_SUBSLOT(o) ((char *)GOBJ_SUB(o))

void actBoySwim(GObj *volatile self)
{
    float pos[4];
    Act *sub = GOBJ_ACT(self);
    int padReq = 0;

    BOY_EXT_F(self, holdBox, word2C0) = 0;
    sub->after = (void *)afterBoySwim;
    while (1) {
        GObj *box = BOY_EXT_F(self, box, holdObj);

        if (sub->curMot == 0xAD) {
            sub->flags20.ll |= 0x800000000ULL;
        }
        if (BOY_EXT_F(self, holdBox, word2C0)) {
            RequestChangeHandMode(self, 0, 3, 1, box, 0, BOY_EXT_F(self, grip, holdPoint));
            BOY_EXT_F(self, grip, holdPoint)[3] = 1.0f;
            sceVu0ApplyMatrix(
                pos, ICO_RAW(void *, GOBJ_SUBSLOT(box), 0xC, *(void **)&GOBJ_SUB(box)->nodeMtx),
                BOY_EXT_F(self, grip, holdPoint));
            debug_NMarker(pos, 0xFF, 0, 0, 100.0f);
            MoveFloatingBox(box, self,
                            *(char **)&GOBJ_SUB(self)->nodeMtx +
                                GetSkeltonFocusNode(self, 0x13) * 0x40 + 0x30,
                            BOY_EXT_F(self, grip, holdPoint), 30.0f);
            if (!(_DistSqGV(test_CURRENTROOT((void *)self), pos) < 4e+04f)) {
                BOY_EXT_F(self, holdBox, word2C0) = 0;
            }
            GOBJ_SUB(self)->root.filter.o.obj = box;
            GOBJ_SUB(self)->root.filter.o.node = -1;
            GOBJ_SUB(self)->root.filter.elem = 0;
            if (!padReq) {
                iosPadActRequest(boyPad, 6);
                padReq = 1;
            }
            if ((int)(sub->flags20.ll >> 35) & 1) {
                if (GOBJ_SUB(self)->ctrl.frameEnd || GOBJ_SUB(self)->ctrl.justShifted) {
                    iosPadActRequest(boyPad, 7);
                }
            }
        } else {
            RequestChangeHandMode(self, 0, 3, 0, 0, 0, 0);
            padReq = 0;
            ((Sub15C *)GOBJ_SUBSLOT(self))->root.filter = InitialColInfo;
        }
        _ACTWait(1);
    }
}

void actBoyWalk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (1) {
        if (ACTGame_FLAG_TETSUNAGI()) {
            float gp[4];
            float bp[4];
            float d;
            float ratio;

            GetSkeltonPosition(bp, boyGObj, 2);
            GetSkeltonPosition(gp, girlGObj, 0x12);
            d = _DistGV((float *)gp, (float *)bp);
            if ((60 - systemStatus[0] * 10) / systemStatus[1] * 100 / 60 < sub->modeFrame &&
                80.0f < d) {
                d = (d - 80.0f) / 10.0f;
                d = d < 0.0f ? 0.0f : (1.0f < d ? 1.0f : d);
                ratio = 0.9 - d * 0.2;
                ACTGame_SetMotionPlaySpeedRatio_Reserve((void *)self, ratio, 2);
            }
        }
        _ACTWait(1);
    }
}

void actBoyRun(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (1) {
        if (ACTGame_FLAG_TETSUNAGI()) {
            float gp[4];
            float bp[4];
            float d;
            float ratio;

            GetSkeltonPosition(bp, boyGObj, 2);
            GetSkeltonPosition(gp, girlGObj, 0x12);
            d = _DistGV((float *)gp, (float *)bp);
            if ((60 - systemStatus[0] * 10) / systemStatus[1] * 100 / 60 < sub->modeFrame &&
                90.0f < d) {
                d = (d - 90.0f) / 10.0f;
                d = d < 0.0f ? 0.0f : (1.0f < d ? 1.0f : d);
                ratio = 0.7 - d * 0.2;
                ACTGame_SetMotionPlaySpeedRatio_Reserve((void *)self, ratio, 2);
            }
        }
        _ACTWait(1);
    }
}

inline void actBoyFall(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter actBoyFall\n");
    sub->actMode = 5;
    while (1) {
        _ACTWait(1);
    }
}

/* the nearest enemy in front of the actor, which ACTSearchEnemy and
   actBoyAttack inline */
static inline void searchEnemy(void *self, ICO_WORD *out_id, float *out_vec) /* derived name */
{
    searchGObj(self, (ICO_RAW(int, self, 0xC, ((GObj *)self)->kind) ^ 1) ? 1 : 4, 0x5A, out_id,
               out_vec, 300.0f);
}

void actBoyAttack(GObj *volatile self)
{
    char *sub = (char *)GOBJ_ACT(self);
    int mot = ((Act *)sub)->frame;
    float vec[4];

    ((Act *)sub)->flags20.ll |= 0x100000000ULL;
    ((Act *)sub)->attackGroup = mot;
    debug_StdPrintfDummy("attack sub id [%d]\n", mot);
    debug_StdPrintfDummy("enter actBoyAttack\n");
    _ACTWait(2);
    searchEnemy((void *)self, &((Act *)sub)->attackTurn, vec);
    while (1) {
        if (((Act *)sub)->attackTurn) {
            if (ACTGame_NoWeapon(self)) {
                SetMotionDirectionWithLimit((void *)self, vec, 5.0f, 45.0f);
            } else {
                SetMotionDirectionWithLimit((void *)self, vec, 10.0f, 90.0f);
            }
        }
        ACTSendMailCorrect(self, 0xC7);
        BoyAttackCenter(self);
        _ACTWait(1);
    }
}

void actBoyTakeWeaponReady(GObj *volatile self)
{
    float w[4];
    float p[4];
    float dir[4];
    GObj *obj;
    int first = 1;
    int n = 0;

    obj = GOBJ_ACT(self)->env.swapWeapon;
    w[0] = test_CURRENTROOT(obj)[0];
    w[1] = test_CURRENTROOT(obj)[1];
    w[2] = test_CURRENTROOT(obj)[2];
    while (1) {
        p[0] = test_CURRENTROOT((void *)self)[0];
        p[1] = test_CURRENTROOT((void *)self)[1];
        p[2] = test_CURRENTROOT((void *)self)[2];
        _OrientXZGV(dir, w, p);
        if (first) {
            SetMotionDirection((void *)self, dir);
            first = 0;
        } else if (_AbsRotyGV(test_CURRENTORIENT((void *)self), dir) < 0x1E) {
            _ACTMotDirSmzDirect((void *)self, dir);
        } else {
            ACTSendMailCorrect(self, 0xCC);
        }
        if ((60 - systemStatus[0] * 10) / systemStatus[1] * 2 < n++) {
            ACTSendMailCorrect(self, 0xCC);
        }
        _ACTWait(1);
    }
}

/* sub->0x14 is the actor's "after" callback slot: actBoyTakeWeapon arms it with
   afterBoyTakeWeapon and calls it through the slot once the motion frame passes
   the swap point, so it is written and read as a function pointer. */
typedef void (*BoyAfterFunc)(GObj *volatile a0);

void actBoyTakeWeapon(GObj *volatile self)
{
    float p[4];
    float dir[4];
    Act *sub = GOBJ_ACT(self);
    int picked = 0;
    int put = 0;

    InitSwapWeapon((void *)self);
    *(BoyAfterFunc *)&sub->after = afterBoyTakeWeapon;
    p[0] = test_CURRENTROOT(BOYINFO.nextWeapon)[0];
    p[1] = test_CURRENTROOT(BOYINFO.nextWeapon)[1];
    p[2] = test_CURRENTROOT(BOYINFO.nextWeapon)[2];
    _OrientXZGV(dir, p, test_CURRENTROOT((void *)self));
    SetMotionDirection((void *)self, dir);
    while (1) {
        if (GOBJ_SUB(self)->ctrl.motion == 0xE6) {
            if (15.0f < GOBJ_SUB(self)->ctrl.animFrame && GOBJ_SUB(self)->ctrl.animFrame < 48.0f &&
                !picked) {
                PickupWeapon(BOYINFO.nextWeapon, (void *)self, 6);
                picked = 1;
            }
            if (30.0f < GOBJ_SUB(self)->ctrl.animFrame && !put) {
                if (BOYINFO.weapon != 0) {
                    ReleaseWeapon(BOYINFO.weapon);
                    ExecuteSEPackage(BOYINFO.weapon, 0x50);
                }
                PutWeapon();
                put = 1;
            }
            if (48.0f < GOBJ_SUB(self)->ctrl.animFrame) {
                if (*(BoyAfterFunc *)&sub->after != 0) {
                    (*(BoyAfterFunc *)&sub->after)(self);
                    *(BoyAfterFunc *)&sub->after = 0;
                }
            }
        } else {
            if (16.0f < GOBJ_SUB(self)->ctrl.animFrame) {
                if (*(BoyAfterFunc *)&sub->after != 0) {
                    (*(BoyAfterFunc *)&sub->after)(self);
                    *(BoyAfterFunc *)&sub->after = 0;
                }
            }
        }
        ACTSendMailCorrect(self, 0xC7);
        _ACTWait(1);
    }
}

#define BOY_WALL(o) ((char *)GOBJ_ACT(o)->work) /* derived name */

void actBoyCliffHesitate(GObj *volatile self)
{
    int hit = 0;

    ACTAdjustPlane(self, &GOBJ_WORK(self)->intrReq.b.wall);
    GetOrientOfWall(ICO_RAWP(char *, BOY_WALL(self), 0x8D0, (char *)GOBJ_WORK(self)->cliffOrient),
                    GOBJ_WORK(self)->intrReq.b.wall.elem, &GOBJ_WORK(self)->intrReq.b.wall.o);
    if (CompareAttribute(((FcWallEnt *)GOBJ_WORK(self)->intrReq.b.wall.elem)->attr, 0x400)) {
        hit = 1;
        GOBJ_WORK(self)->cliffReq.b = GOBJ_WORK(self)->intrReq.b;
    }
    while (1) {
        if (hit) {
            ACTSendMailCorrect(self, 0x8C);
        }
        if (girlGObj != 0) {
            brainSetSpMode();
            if (girlGObj != 0) {
                iosOmSendMail(girlGObj, 0x3D, isysCurrentGObj);
            }
        }
        ACTSendMailCorrect(self, 0x128);
        _ACTWait(1);
    }
}

inline void actBoyCall(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter actBoyCall\n");
    sub->actMode = 9;
    _ACTWait(2);
    if (girlGObj != 0) {
        iosOmSendMail(girlGObj, 0x41, isysCurrentGObj);
    }
    while (1) {
        if ((sub->pad.now & 8) == 0) {
            ACTSendMailCorrect(self, 0xC7);
        }
        _ACTWait(1);
    }
}

#define BOY_GIRL_DY()                                                                              \
    (test_CURRENTROOT(girlGObj)[1] - test_CURRENTROOT(boyGObj)[1]) /* derived name */

static void ACTSendMail_PULLUP_GO(void)
{
    GObj *g = boyGObj;
    Act *sub = GOBJ_ACT(g);

    switch (sub->env.cliffSel) {
    case 0x64:
        ACTSendMailCorrect(g, 0x4A);
        sub->orientMot = 0x6B;
        break;
    case 0xC8:
        ACTSendMailCorrect(g, 0x4A);
        sub->orientMot = 0x6D;
        break;
    case 0x12C:
        if (GOBJ_ACT(girlGObj)->actMode != 0x50) {
            break;
        }
        if (!(300.0f < (BOY_GIRL_DY() < 0.0f ? -BOY_GIRL_DY() : BOY_GIRL_DY()))) {
            ACTSendMailCorrect(g, 0x4A);
            sub->orientMot = 0x6F;
        } else if (girlGObj != 0) {
            iosOmSendMail(girlGObj, 0x52, isysCurrentGObj);
        }
        break;
    }
}

/* the pull-up start mail, inlined into actBoyPullupGo */
static inline void ACTSendMail_PULLUP_START(void) /* derived name */
{
    Act *sub = GOBJ_ACT(boyGObj);

    switch (sub->env.cliffSel) {
    case 0x64:
        if (girlGObj != 0) {
            iosOmSendMail(girlGObj, 0x51, isysCurrentGObj);
        }
        GOBJ_ACT(girlGObj)->orientMot = 0x65;
        break;
    case 0xC8:
        if (girlGObj != 0) {
            iosOmSendMail(girlGObj, 0x51, isysCurrentGObj);
        }
        GOBJ_ACT(girlGObj)->orientMot = 0x66;
        break;
    case 0x12C:
        if (girlGObj != 0) {
            iosOmSendMail(girlGObj, 0x51, isysCurrentGObj);
        }
        GOBJ_ACT(girlGObj)->orientMot = 0x67;
        break;
    }
}

static int pullup_check_heroin_position(void)
{
    float buf[4];
    float p1[4];
    float p2[4];
    GObj *g = boyGObj;
    Act *s = GOBJ_ACT(g);

    if (girlControlMode != 0 && s->env.cliffSel == 300) {
        switch (GOBJ_ACT(girlGObj)->actMode) {
        case 4:
            GetSkeltonPosition(p1, girlGObj, 0x16);
            GetSkeltonPosition(p2, boyGObj, 6);
            if (_DistSqGV(p1, p2) < 3600.0f) {
                if (girlGObj != 0) {
                    iosOmSendMail(girlGObj, 0x59, isysCurrentGObj);
                }
            }
            return 0;
        case 0x51:
            ACTSendMailCorrect(boyGObj, 0x58);
            return 0;
        default:
            return 0;
        }
    }
    sceVu0SubVector(buf, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    if (0.0f < sceVu0InnerProduct(buf, s->env.cliffOrient) &&
        _DistxzGV(s->env.cliffStepPos, test_CURRENTROOT(girlGObj)) < 100.0f &&
        ((unsigned int)(GOBJ_ACT(girlGObj)->flags18.ll >> 54) & 1) &&
        (girlGObj == 0 || boyGObj == 0 ||
         !(test_CURRENTROOT(girlGObj)[1] > test_CURRENTROOT(boyGObj)[1] + 450.0f))) {
        return 1;
    }
    return 0;
}

static int ditch_check_heroin_position(void)
{
    float buf[4];
    Act *s = GOBJ_ACT(boyGObj);

    sceVu0SubVector(buf, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    if (0.0f < sceVu0InnerProduct(buf, (float *)s->env.cliffOrient) &&
        _DistxzGV((char *)s->env.ditchPos, test_CURRENTROOT(girlGObj)) < 31.0f &&
        (girlGObj == 0 || boyGObj == 0 ||
         !(test_CURRENTROOT(girlGObj)[1] > test_CURRENTROOT(boyGObj)[1] + 200.0f))) {
        return 1;
    }
    return 0;
}

static unsigned char isGirlWithinPullupHeight(void) /* derived name */
{
    float boy[4];
    float girl[4];

    GetRootProjectionPosOfGObj(boy, boyGObj);
    GetRootProjectionPosOfGObj(girl, girlGObj);
    if (GOBJ_ACT(girlGObj)->actMode == 0x26 ||
        (boy[1] - girl[1] < 0.0f ? -(boy[1] - girl[1]) : boy[1] - girl[1]) < 50.0f) {
        return 1;
    }
    return 0;
}

void actBoyPullupReady(GObj *volatile self)
{
    float mv[4];

    Act *sub = GOBJ_ACT(self);

    ACTAdjustPlane(
        self, ICO_RAWP(char *, BOY_WALL(self), 0x8C0, (char *)&GOBJ_WORK(self)->intrReq.b.wall));
    while (1) {
        if (GOBJ_WORK(self)->boxSideSet && motionKind[GOBJ_SUB(self)->ctrl.motion].playMode != 1) {
            _MoveGV(mv, test_CURRENTROOT(self), GOBJ_WORK(self)->boxSidePos, 3.0f);
            SetRootPosition(self, mv);
        }
        _ACTCharStatus_Set(self, 0x1C, -1.0f, 0);
        if ((sub->pad.now & 8) == 0 || isGirlWithinPullupHeight()) {
            ACTSendMailCorrect(self, 0x49);
        } else if (pullup_check_heroin_position()) {
            if (PAIR_IsStatus_GIRL_PULL() == 0) {
                if (!(300.0f < (BOY_GIRL_DY() < 0.0f ? -BOY_GIRL_DY() : BOY_GIRL_DY()) &&
                      GOBJ_WORK(girlGObj)->jumpTimer)) {
                    if (girlGObj != 0) {
                        iosOmSendMail(girlGObj, 0x4F, isysCurrentGObj);
                    }
                }
            } else if (IsCorrectPosition(girlGObj) == 0) {
                ACTSendMail_PULLUP_GO();
            }
        }
        _ACTWait(1);
    }
}

/* the boy is hauling the girl up: moving while the stick is pushed between
   0.1 and 0.99 of its range, or further with button 0x20 held */
#define BOY_PULLUP_MOVING(sub) /* derived name */                                                  \
    (0.1f < (sub)->stick.mag && ((sub)->stick.mag < 0.99f || ((sub)->pad.now & 0x20)))

void actBoyPullupGo(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    ACTSendMail_PULLUP_START();
    while (1) {
        if (PAIR_IsStatus_GIRL_PULL() == 0) {
            ACTSendMailCorrect(self, 0x4B);
        } else {
            if (0.1f < sub->stick.mag && !BOY_PULLUP_MOVING(sub)) {
                ACTSendMailCorrect(self, 0x4C);
            } else if (BOY_PULLUP_MOVING(sub)) {
                ACTSendMailCorrect(self, 0x4D);
            } else {
                ACTSendMailCorrect(self, 0x4E);
            }
        }
        _ACTWait(1);
    }
}

extern void InsertCamera_Set(float *pos, float *tgt, int frames);

void actBoyBelift(GObj *volatile self)
{
    /* the quaternion is reached through a union, the form actEnemyKidnapEnd
       (enemy_act.c) gives the same memset-and-w=1 idiom */
    union { /* field names derived */
        float f[4];
    } q;

    float cam[4];
    float boypos[4];
    float p[4];
    float girlpos[4];
    float dir[4];
    float ofs[4];
    float hit[4];
    GObj *girl = GOBJ_ACT(self)->intrArg;
    float ratio;
    int mode;
    int lv;
    float dist;

    memset(&q, 0, 0x10);
    q.f[3] = 1.0f;
    ratio = 1.0f;
    if (GOBJ_ACT(girl)->enemy->liftKind == 3) {
        ratio = 0.2f;
    }
    mode = GOBJ_ACT(girl)->enemy->liftKind == 3 ? 0 : 2;
    BOY_EXT_F(self, liftObj, liftedObj) = girl;
    beliftGirl = girl;
    BOY_EXT_F(self, liftLevel, liftLevel) = 10;
    if (GOBJ_ACT(girl)->enemy->liftKind == 3) {
        RotQuaternionX(q.f, 0x4000);
    }
    SetMotionNodeFixModeParameter((void *)self, girl, mode, 0x16, q.f, 0.0f, 0.0f, 0.0f, ratio);
    cam[0] = GetCurrentCameraSet2()[0];
    cam[1] = GetCurrentCameraSet2()[1];
    cam[2] = GetCurrentCameraSet2()[2];
    _ACTWait(1);
    while (1) {
        if (GOBJ_ACT(girl)->enemy->liftKind == 3 && (unsigned int)GOBJ_ACT(girl)->actMode == 0x61) {
            lv = BOY_EXT_F(self, liftLevel, liftLevel);
            lv = lv < 0 ? 0 : (10.0f < lv ? 10.0f : lv);
            lv = lv * 0.5f;
            dist = lv * 100.0f + 500.0f;
            GetSkeltonPosition(boypos, boyGObj, 0x23);
            GetSkeltonPosition(girlpos, girl, 0x23);
            _OrientXZGV(dir, girlpos, boypos);
            sceVu0ScaleVector(ofs, dir, -dist);
            sceVu0AddVector(p, ofs, boypos);
            if (_DistSqGV(p, cam) < 2500.0f) {
                _InterGV(p, cam, p, 1.0f, 1.0f);
            } else {
                _MoveGV(p, cam, p, 50.0f);
            }
            if (ACTCheckCollis_WF(50.0f, girlpos, p, 0, hit)) {
                p[0] = hit[0];
                p[1] = hit[1];
                p[2] = hit[2];
            }
            cam[0] = p[0];
            cam[1] = p[1];
            cam[2] = p[2];
            GetSkeltonPosition(girlpos, boyGObj, 0x2C);
            sceVu0ScaleVector(p, p, -1.0f);
            sceVu0ScaleVector(girlpos, girlpos, -1.0f);
            InsertCamera_Set(p, girlpos, (60 - systemStatus[0] * 10) / systemStatus[1] / 6);
        }
        switch ((unsigned int)GOBJ_ACT(girl)->actMode) {
        case 0x61:
        case 0x62:
            break;
        default:
            ACTSendMailCorrect(self, 0xE2);
            debug_StdPrintfDummy("enemy error body slam[%s]\n",
                                 actModeTbl[(unsigned int)GOBJ_ACT(girl)->actMode].name);
            break;
        }
        _ACTWait(1);
    }
}

/* The walk order the boy is executing: sub->0x30 points at the request record
   the caller filled in, and actBoyReadyMove works on a private copy of it. */
typedef struct { /* field names derived */
    float pos[4];
    float dir[4];
    float range;
    int frames;
    int fix;
    int _2C;
} __attribute__((aligned(16))) BoyMoveOrder; /* derived name */

void actBoyReadyMove(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    BoyMoveOrder ord = *(BoyMoveOrder *)((char *)GOBJ_ACT(self)->intrData);

    while (1) {
        if ((((void *)self == girlGObj && girlControlMode != 0)
                 ? (GOBJ_SUB(self)->ctrl.motion + motionKind)->girlDirFrames
                 : (GOBJ_SUB(self)->ctrl.motion + motionKind)->dirFrames) == 0) {
            SetMotionDirectionSmooze(self, ord.dir, 10.0f);
        } else {
            _ACTMotDirSmzDirect((void *)self, ord.dir);
        }
        if (ord.fix) {
            sub->wish3.ll |= 0x80000;
            sub->wish3.ll |= 0x40;
        }
        if (ord.frames-- < 0) {
            ACTSendMailCorrect(self, 0x10A);
        }
        if (_DistxzSqGV(test_CURRENTROOT((void *)self), &ord) < ord.range * ord.range) {
            ACTSendMailCorrect(self, 0x10A);
        }
        if (0.1f < sub->stick.mag && 100 < _AbsRotyGV(ord.dir, (char *)sub->dir)) {
            ACTSendMailCorrect(self, 0x10A);
        }
        _ACTWait(1);
    }
}

void actBoyRescueReady(GObj *volatile self)
{
    GObj *g = GOBJ_ACT(self)->enemy->rescueObj;
    float p[4];
    float q[4];
    float step[4];
    float dir[4];
    float tmp[4];
    float np[4];

    union { /* field names derived */
        float f[4];
        long long ll[2];
    } pts[2];

    int cnt = (60 - systemStatus[0] * 10) / systemStatus[1] / 3;
    int rest, hp, r;
    int n1, n2;
    char *hold;
    int u = 0;
    int gm = GOBJ_SUB(girlGObj)->ctrl.motion;
    int t;

    p[0] = test_CURRENTROOT((void *)self)[0];
    p[1] = test_CURRENTROOT((void *)self)[1];
    p[2] = test_CURRENTROOT((void *)self)[2];
    q[0] = test_CURRENTROOT(g)[0];
    q[1] = test_CURRENTROOT(g)[1];
    q[2] = test_CURRENTROOT(g)[2];
    _OrientXZGV(dir, q, p);
    sceVu0ScaleVector(tmp, dir, -60.0f);
    sceVu0AddVector(GOBJ_ACT(self)->enemy->rescueBoyPos, q, tmp);
    sceVu0ScaleVector(tmp, dir, 0.0f);
    sceVu0AddVector(GOBJ_ACT(self)->enemy->rescueGirlPos, q, tmp);
    GOBJ_ACT(self)->enemy->rescueGirlPos[1] += 50.0f;
    SetMotionDirection((void *)self, dir);
    sceVu0SubVector(step, GOBJ_ACT(self)->enemy->rescueBoyPos, (float *)p);
    sceVu0ScaleVector(step, step, 1.0f / (float)cnt);
    t = 1;
    rest = cnt;
    while (1) {
        if (0 < rest) {
            sceVu0AddVector(np, test_CURRENTROOT((void *)self), step);
            np[1] = test_CURRENTROOT((void *)self)[1];
            SetDirectRootPositionNoFitting((void *)self, np);
        }
        rest--;
        if (motionKind[GOBJ_SUB(self)->ctrl.motion].playMode == 1 ||
            (GOBJ_SUB(self)->ctrl.ctrlFlags & 0x16) || GOBJ_SUB(self)->ctrl.frameEnd != 0) {
            hold = (char *)GOBJ_ACT(girlGObj)->carrier;
            hp = GOBJ_ACT(hold)->modeFrame;
            r = 0;
            /* n1 takes systemStatus[0] and is not read on this path */
            n1 = systemStatus[0];
            ACTGame_ConnectHand();
            if ((60 - systemStatus[0] * 10) / systemStatus[1] * 100 / 60 <= hp) {
                r = hp * ((60 - systemStatus[0] * 10) / systemStatus[1] * 3) /
                    ((60 - systemStatus[0] * 10) / systemStatus[1] * 10);
            }
            /* the quotient is not read */
            if (0 <= r) {
                r = (60 - systemStatus[0] * 10) / systemStatus[1];
            }
            if (gameover_flag == 0) {
                if (girlGObj != 0) {
                    iosOmSendMail(girlGObj, 0x15D, isysCurrentGObj);
                }
            }
            n1 = GetSkeltonFocusNode(girlGObj, 0x16);
            n2 = GetSkeltonFocusNode(boyGObj, 6);
            pts[0].f[0] = *(float *)((char *)GOBJ_SUB(girlGObj)->nodeMtx + n1 * 0x40 + 0x30);
            pts[0].f[1] = *(float *)((char *)GOBJ_SUB(girlGObj)->nodeMtx + n1 * 0x40 + 0x34);
            pts[0].f[2] = *(float *)((char *)GOBJ_SUB(girlGObj)->nodeMtx + n1 * 0x40 + 0x38);
            pts[1].f[0] = *(float *)((char *)GOBJ_SUB(boyGObj)->nodeMtx + n2 * 0x40 + 0x30);
            pts[1].f[1] = *(float *)((char *)GOBJ_SUB(boyGObj)->nodeMtx + n2 * 0x40 + 0x34);
            pts[1].f[2] = *(float *)((char *)GOBJ_SUB(boyGObj)->nodeMtx + n2 * 0x40 + 0x38);
            SetDirectRootPositionNoFittingWithNodePoint((void *)self, 6, pts[0].f, 0.05f);
            u = t++;
        }
        if (gm == 641) {
            if ((u / 30) & 1) {
                ACTSendMailCorrect(self, 0x160);
            } else {
                ACTSendMailCorrect(self, 0x15F);
            }
        } else {
            if ((u / 15) & 1) {
                ACTSendMailCorrect(self, 0x160);
            } else {
                ACTSendMailCorrect(self, 0x15F);
            }
        }
        _ACTWait(1);
    }
}

void actBoyDitch3mReady(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    float p[4];
    float q[4];
    int c = 0;
    int a;
    int b;

    ACTAdjustPlane(
        self, ICO_RAWP(char *, BOY_WALL(self), 0x8C0, (char *)&GOBJ_WORK(self)->intrReq.b.wall));
    _ACTWait(1);
    ResetMotionProgramInterpInfo(self, 35);
    ResetMotionProgramInterpInfo(self, 1);

    while (1) {
        a = 1;
        b = 0;
        switch ((unsigned int)GOBJ_ACT(girlGObj)->actMode) {
        case 4:
        case 90:
            p[0] = test_CURRENTROOT(boyGObj)[0];
            p[1] = test_CURRENTROOT(boyGObj)[1];
            p[2] = test_CURRENTROOT(boyGObj)[2];
            q[0] = test_CURRENTROOT(girlGObj)[0];
            q[1] = test_CURRENTROOT(girlGObj)[1];
            q[2] = test_CURRENTROOT(girlGObj)[2];
            if (_DistxzSqGV(p, q) < 250000.0f) {
                if (p[1] + 300.0f < q[1]) {
                } else {
                    a = 0;
                }
            }
            break;
        case 28:
        case 29:
            b = 1;
            break;
        }

        if (GOBJ_ACT(girlGObj)->actMode != 4 &&
            _DistSqGV(test_CURRENTROOT(boyGObj), (char *)sub->env.ditchPos) < 90000.0f &&
            _DistSqGV(test_CURRENTROOT(girlGObj), (char *)sub->env.ditchPos) < 90000.0f) {
            c = 1;
        }

        if (c != 0) {
            if (GOBJ_ACT(girlGObj)->actMode == 4) {
                GOBJ_WORK(self)->ditchTimer = (60 - systemStatus[0] * 10) / systemStatus[1] / 3;
            }
            _ACTParaStatus_Set((void *)self, 40);
            _ACTCharStatus_Set((void *)self, 29, -1.0f, 0);
            GOBJ_WORK(self)->parallelInterp = 30.0f;
            ACTSendMailCorrect(self, 0x187);
        }

        /* two branches with one body */
        if (a != 0 && (sub->pad.now & 8) == 0) {
            ACTSendMailCorrect(self, 0x189);
            if (c != 0) {
                ACTSendMailCorrect(self, 0x18A);
            }
        } else if (b != 0) {
            ACTSendMailCorrect(self, 0x189);
            if (c != 0) {
                ACTSendMailCorrect(self, 0x18A);
            }
        } else {
            if (girlControlMode == 0) {
                if (ditch_check_heroin_position() != 0) {
                    if (girlGObj != 0) {
                        iosOmSendMail(girlGObj, 0x18C, isysCurrentGObj);
                    }
                }
                if (IsCorrectPosition(girlGObj) == 0) {
                    if (girlGObj != 0) {
                        iosOmSendMail(girlGObj, 0x18D, isysCurrentGObj);
                    }
                }
            }
            if (GOBJ_ACT(girlGObj)->actMode == 4 && c == 0) {
                float dy;

                b = 0;
                dy = test_CURRENTROOT(girlGObj)[1] - test_CURRENTROOT(boyGObj)[1];
                if (_DistxzSqGV(test_CURRENTROOT(boyGObj), test_CURRENTROOT(girlGObj)) < 10000.0f &&
                    ABSF(BOYGIRL_DY()) < 100.0f) {
                    b = 1;
                }
                if (_DistxzSqGV(test_CURRENTROOT(boyGObj), test_CURRENTROOT(girlGObj)) < 78400.0f &&
                    ABSF(BOYGIRL_DY()) < 200.0f && 50.0f < dy && dy < 200.0f) {
                    b = 1;
                }
                GetSkeltonPosition(p, girlGObj, 22);
                GetSkeltonPosition(q, boyGObj, 6);
                if (_DistSqGV(p, q) < 3600.0f) {
                    b = 1;
                }
                if (sub->env.ditchCarry != 0) {
                    b = 0;
                }
                if (b != 0) {
                    if (girlGObj != 0) {
                        iosOmSendMail(girlGObj, 0x18F, isysCurrentGObj);
                    }
                    ACTSendMailCorrect(self, 0x18B);
                }
            }
        }
        _ACTWait(1);
    }
}

void actBoyRescueGirlBhang(GObj *volatile self)
{
    float tgt[4];
    float mv[4];
    float boy[4];
    float girl[4];
    Act *sub = GOBJ_ACT(self);
    int mode;
    int connect = 1;

    ACT_AFTER_PROC(sub) = (void (*)(GObj *))afterBoyRescueGirlBhang;
    while (1) {
        mode = 0;
        switch (GOBJ_ACT(girlGObj)->actMode) {
        case 0x1C:
            mode = 1;
            break;
        case 0x1D:
            mode = 2;
            if ((60 - systemStatus[0] * 10) / systemStatus[1] / 2 < sub->modeFrame) {
                if (connect) {
                    ACTGame_ConnectHand();
                    connect = 0;
                }
            }
            break;
        }
        if (mode != 0) {
            if (sub->modeFrame < 0xA) {
                boy[0] = test_CURRENTROOT(boyGObj)[0];
                boy[1] = test_CURRENTROOT(boyGObj)[1];
                boy[2] = test_CURRENTROOT(boyGObj)[2];
                girl[0] = test_CURRENTROOT(girlGObj)[0];
                girl[1] = test_CURRENTROOT(girlGObj)[1];
                girl[2] = test_CURRENTROOT(girlGObj)[2];
                tgt[0] = girl[0];
                tgt[2] = girl[2];
                tgt[1] = boy[1];
                _MoveGV(mv, boy, tgt, 5.0f);
                SetRootPosition((void *)self, mv);
            }
        }
        if (motionKind[GOBJ_SUB(self)->ctrl.motion].playMode == 1) {
            if (mode == 1) {
                ACTSendMailCorrect(self, 0x15B);
                if (girlGObj != 0) {
                    iosOmSendMail(girlGObj, 0x51, isysCurrentGObj);
                }
                GOBJ_ACT(girlGObj)->orientMot = 0x66;
            }
            if (mode == 2) {
                ACTSendMailCorrect(self, 0x15B);
                if (girlGObj != 0) {
                    iosOmSendMail(girlGObj, 0x40, isysCurrentGObj);
                }
            }
        }
        if (mode == 0) {
            ACTSendMailCorrect(self, 0xC7);
        }
        _ACTWait(1);
    }
}

inline int RequestStageChangeKidnapEnd(int stage, int targetId)
{
    int rv = 0;
    if (boyGObj != 0) {
        rv = RequestStageChangeSimple(stage, 0.25f, 4.0f, 0, 0, 0) & 0xFF;
        if (rv != 0) {
            Vec16 buf = {{-1000000.0f, 0.0f, 0.0f}};

            BOYEFSTAGE[0] = 1;
            *(int *)(BOYEFSTAGE + 4) = targetId;
            ACTGame_StageChangeGObjDirect(boyGObj, stage, &buf, 0);
        }
    }
    return rv;
}

/* as in camera-root.h, which this TU does not include (GetCurrentCameraSet2 differs) */
extern void InsertCamera_SetNoraml(float *pos, float *tgt, int frames, int cutType);

void SetStatusBoy_OtherStageGirlPinch(void)
{
    float buf[4];
    float cam[4];
    float pos[4];
    GObj *g = boyGObj;
    ActWork *w;
    float a;
    float b;
    float t;
    int frames;

    a = _ACTGame_GetParamF(0xD);
    b = _ACTGame_GetParamF(0xE);
    t = a * (float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f +
        b * (float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f;
    ExecuteSEPackage(0, 0x7F);
    GetOtherStageGirlOrient(buf, GetCurrentCameraSet2());
    w = GOBJ_WORK(g);
    frames = (int)t;
    w->pinchFrames = frames;
    w->pinchPosX = buf[0];
    w->pinchPosY = buf[1];
    w->pinchPosZ = buf[2];
    cam[0] = GetCurrentCameraSet2()[0];
    cam[1] = GetCurrentCameraSet2()[1];
    cam[2] = GetCurrentCameraSet2()[2];
    pos[0] = test_CURRENTROOT(boyGObj)[0];
    pos[1] = test_CURRENTROOT(boyGObj)[1];
    pos[2] = test_CURRENTROOT(boyGObj)[2];
    InsertCamera_SetNoraml(cam, pos, frames, 0);
}

void *gopp_subBoyControl;

void actBoyStart(GObj *self)
{
    char *work;
    void *g;

    girlEscortedInStage = BOYINFO.escort;
    BOYINFO.escort = 0;

    ((int *)boyInfo)[4] = -1;

    ableBoyControl = 0;
    startWithGirl = 0;
    startWaitCancel = 0;
    startOnSofa = 0;
    sitLayoutDone = 0;

    sitCount = 0;
    debug_StdPrintfDummy("actBoyStart:%p\n", self);

    work = (char *)actInitialize(self);
    ((Act *)work)->addData = attrWallHit;

    GetRootPosition((char *)&((Act *)work)->camRootX, (void *)self);

    actInitialize_ext_charcter(self);
    actInitialize_only_charcter((char *)self);
    actInitialize_geo(self);

    GOBJ_ACT(self)->enemy->sofaWakeTime =
        (int)(_ACTGame_GetParamF(0x21) * (float)((60 - systemStatus[0] * 10) / systemStatus[1]) /
              60.0f);
    ACTGame_LwsEffectInit(self);

    ((Act *)work)->heldItem.i = 0;
    ((Act *)work)->nextItem.i = 0;

    gopp_subBoyControl = 0;
    ACTParaStatus_Init(self);
    _ACTCharStatus_Init((int **)self);

    _ACTWait(1);

    CurrentTargetGObj = (void *)self;

    ((Act *)work)->life = _ACTGame_GetParamF(0x13);
    ((Act *)work)->actKind = 0;

    if (BOYINFO.boyID != 0) {
        g = isysGObjSearchFromObjLayoutID(BOYINFO.boyID);
        if (g != 0) {
            PickupWeapon(g, (void *)self, 0x16);
            ((Act *)work)->weapon = g;
            if (BOYINFO.torch) {
                LightTorchOnOfWeaponWithNoSE(g);
            }
        } else {
            BOYINFO.boyID = 0;
        }
    }

    if (BOYINFO.girlID != 0) {
        g = isysGObjSearchFromObjLayoutID(BOYINFO.girlID);
        if (g != 0) {
            ACTSendMailCorrect(self, 0x35);
            ((Act *)work)->nextItem.p = ((Act *)work)->curItem = g;
        } else {
            BOYINFO.girlID = 0;
        }
        startWithGirl = 1;
    }

    if (BOYINFO.layoutID != 0) {
        g = isysGObjSearchFromObjLayoutID(BOYINFO.layoutID);
        if (g != 0) {
            Vec16 p0 = {{0.0f, 0.0f, -50.0f, 1.0f}};
            Vec16 p1 = {{0.0f, 0.0f, 50.0f, 1.0f}};
            ClipWork cw;

            CopyMatrix(MatrixDrive_GetMatrix(), (void *)GOBJ_SUB(g)->nodeMtx);
            MatrixDrive_TransMatrix(0.0f, -50.0f, 0.0f);
            sceVu0ApplyMatrix(cw.pt[0], MatrixDrive_GetMatrix(), &p0);
            sceVu0ApplyMatrix(cw.pt[1], MatrixDrive_GetMatrix(), &p1);
            cw.radius = 0.0f;
            ClipWall(&cw);
            if (cw.wall.elem == 0) {
                /* "!!! cannot find the sofa's wall !!!" */
                debug_StdPrintfDummy("！！！ソファの壁を見付けることができません！！！\n");
            } else {
                startOnSofa = 1;
                sitLayoutDone = 1;
                sofaWallHit.o = cw.wall.o;
                sofaWallHit.elem = cw.wall.elem;
                scpBoyControlReadDisable = 0;
                ActSendMail_WithAdditionalData((void *)self, 0x36, (void *)self, &sofaWallHit);
                if (BOYINFO.bit32 && girlGObj != 0) {
                    ActSendMail_WithAdditionalData(girlGObj, 0x36, girlGObj, &sofaWallHit);
                }
            }
        } else {
            BOYINFO.layoutID = 0;
        }
    }

    startWaitCancel = 0;

    ((Act *)work)->mainMail = (ActMail *)&actIntrList[74];

    actCreateSubThread(subBoyBrainMain, 20);
    gopp_subBoyControl = (void *)actCreateSubThread(subBoyControl, 21);
    actCreateSubThread(subBoyCollision, 21);
    actCreateSubThread(subCommonIdle, 21);

    ((Act *)work)->mail = (ActMail *)&actIntrList[79];
    ACTSendMailCorrect(self, 0xC7);

    _ACTWait(1);

    if (BOYINFO.fire && girlGObj != 0) {
        iosOmSendMail(girlGObj, 0x3F, self);
        debug_StdPrintfDummy("hand connect start\n");
    }
    _ACTWait(0);
}

inline int CorrectStickInfo(void *dir, IosPadStick *stick)
{
    return correctStick(dir, stick);
}

inline void *GetBoyWeaponGObj(void)
{
    GObj *g = boyGObj;
    if (g != 0) {
        return (void *)GOBJ_ACT(g)->weapon;
    }
    return 0;
}

inline void actBoyStand(GObj *volatile self)
{
    MotionDef *row = GOBJ_SUB(self)->ctrl.motion + motionKind;

    if ((row->flags.word >> 8) & 1) {
        ACTAdjustPlane(self, &GOBJ_WORK(self)->intrReq.a.wall);
    }
    while (1) {
        _ACTWait(1);
    }
}

inline void actBoyHang(GObj *volatile self)
{
    char *g = (char *)self;
    ACTAdjustPlane(self, &GOBJ_WORK(g)->intrReq.a.wall);
    _ACTWait(0);
}

inline void actBoyBHang(GObj *volatile self)
{
    char *g = (char *)self;
    ACTAdjustPlane(self, &GOBJ_WORK(g)->intrReq.a.wall);
    _ACTWait(0);
}

inline void actBoyHangBefore(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    ACTAdjustPlane(self, &GOBJ_WORK(self)->intrReq.b.wall);
    GOBJ_WORK(self)->hangOrient[0] = sub->env.cliffOrient[0];
    GOBJ_WORK(self)->hangOrient[1] = sub->env.cliffOrient[1];
    GOBJ_WORK(self)->hangOrient[2] = sub->env.cliffOrient[2];
    GOBJ_WORK(self)->cliffReq.a = sub->env.motOriReq.b;
    while (1) {
        ACTSendMailCorrect(self, 0x128);
        _ACTWait(1);
    }
}

inline void actBoyBeslam(GObj *volatile self)
{
    char *p = (char *)GOBJ_SUB(self)->root.move;

    sceVu0ScaleVector(p, test_CURRENTORIENT(beliftGirl), 30.0f);
    while (1) {
        ACTSendMailCorrect(self, 0x13A);
        _ACTWait(1);
    }
}

inline void actBoyRescueSrc(GObj *volatile self)
{
    while (1) {
        ACTSendMailCorrect(self, 0xC7);
        _ACTWait(1);
    }
}

inline void actBoySupportGBBegin(GObj *volatile self)
{
    while (1) {
        if (girlGObj != 0) {
            iosOmSendMail(girlGObj, 0x17D, isysCurrentGObj);
        }
        ACTSendMailCorrect(self, 0x17B);
        _ACTWait(1);
    }
}

static inline unsigned char IsBoyStatus_SupportGB(void) /* derived name */
{
    unsigned int st = (unsigned int)GOBJ_ACT(girlGObj)->actMode;
    if (st < 0x6B) {
        if (st >= 0x68) {
            return 1;
        }
    }
    return 0;
}

inline void actBoySupportGBLoop(GObj *volatile self)
{
    while (1) {
        if (!IsBoyStatus_SupportGB()) {
            ACTSendMailCorrect(self, 0xE2);
        }
        _ACTWait(1);
    }
}

inline void actBoySupportGBEnd(GObj *volatile self)
{
    while (1) {
        if (girlGObj != 0) {
            iosOmSendMail(girlGObj, 0x17F, isysCurrentGObj);
        }
        ACTSendMailCorrect(self, 0xC7);
        _ACTWait(1);
    }
}

inline void actBoySupportBGBegin(GObj *volatile self)
{
    while (1) {
        ACTSendMailCorrect(self, 0x183);
        _ACTWait(1);
    }
}

inline void actBoyDitch3mExec(GObj *volatile self)
{
    while (1) {
        ACTSendMailCorrect(self, 0x190);
        _ACTWait(1);
    }
}

inline void actBoyHangG3M(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    ACT_AFTER_PROC(sub) = (void (*)(GObj *))afterBoyHangG3M;
    while (1) {
        if (0.1f < sub->stick.mag || (sub->pad.now & 0x10)) {
            ACTSendMailCorrect(self, 0x192);
            if (girlGObj != 0) {
                iosOmSendMail(girlGObj, 0x195, isysCurrentGObj);
            }
        }
        _ACTWait(1);
    }
}

inline unsigned char IsAbleBoyControl(void)
{
    return ableBoyControl;
}

inline void ACTSearchEnemy(void *self, ICO_WORD *out_id, float *out_vec)
{
    searchEnemy(self, out_id, out_vec);
}

inline void DeleteBoyWeapon(void)
{
    union { /* field names derived */
        float f[4];
        long long ll[2];
    } buf;

    Act *sub;

    if (boyGObj != 0) {
        sub = GOBJ_ACT(boyGObj);
        if ((void *)sub->weapon != 0) {
            ReleaseWeapon((void *)sub->weapon);
            memset(&buf, 0, 0x10);
            buf.f[0] = 10000000.0f;
            SetDirectRootPositionNoFitting((void *)sub->weapon, buf.f);
            gamesysObjInfoPosSetStage(sub->weapon, 0, 0, stage_no);
            ((GObj *)sub->weapon)->active = 0;
        }
        characterPacket[2] = 0;
        ((int *)boyInfo)[0] = 0;
        sub->weapon = 0;
    }
}

inline int isLiftBoyEnable(void)
{
    unsigned int st = (unsigned int)GOBJ_ACT(boyGObj)->actMode;
    if (st >= 0x60) {
        return 1;
    }
    if (st < 0x5E) {
        return 1;
    }
    return 0;
}

inline void SetKidnapInfo(int enemyLabel, int targetLabel)
{
    characterPacket[5] = enemyLabel;
    characterPacket[6] = targetLabel;
}

inline void GetKidnapInfo(int *enemyLabel, int *targetLabel)
{
    *enemyLabel = characterPacket[5];
    *targetLabel = characterPacket[6];
}

inline void PrivInsCamSet(float *pos, float *tgt, GObj *track, int inFrames, int outFrames,
                          float inRate, float blend, unsigned char control)
{
    privInsCam.pos[0] = pos[0];
    privInsCam.pos[1] = pos[1];
    privInsCam.pos[2] = pos[2];
    privInsCam.tgt[0] = tgt[0];
    privInsCam.tgt[1] = tgt[1];
    privInsCam.tgt[2] = tgt[2];
    privInsCam.track = track;
    privInsCam.inFrames = inFrames;
    privInsCam.outFrames = outFrames;
    privInsCam.inRate = inRate;
    privInsCam.blend = blend;
    privInsCam.control = control;
    privInsCam.on = 1;
}

inline int IsBoyStatus_EnemyMustWait(void)
{
    Act *sub;
    unsigned int st;

    if (boyGObj == 0) {
        debug_assert(__FILE__, 5795);
        __assert(__FILE__, 5795, "0");
        return 0;
    }
    sub = GOBJ_ACT(boyGObj);
    st = (unsigned int)sub->actMode;
    if (st >= 0x13) {
        if (st >= 0x16) {
            if (st < 0x18) {
                return 1;
            }
        } else if (sub->enemy->stoneLevel != 0) {
            return 1;
        }
    }
    if (st == 0x15 || 0 < ((ActWork *)sub->work)->downTimer) {
        return 1;
    }
    return 0;
}

inline int IsGirlEscortedInNextStage(void)
{
    return (int)((unsigned char)((unsigned long long)boyInfo[1] >> 35)) & 1;
}

inline unsigned char IsGirlEscortedInCurrentStage(void)
{
    return girlEscortedInStage;
}

inline int GetSaveSofaLayoutID(void)
{
    Act *pa;
    Act *pb;

    if (boyGObj == 0 || girlGObj == 0) {
        return -1;
    }
    pa = GOBJ_ACT(boyGObj);
    if (pa->actMode != 0x2D) {
        return -1;
    }
    pb = GOBJ_ACT(girlGObj);
    if (pb->actMode != pa->actMode) {
        return -1;
    }
    return ((GObj *)pa->sofa)->labelId;
}

inline void OnGirlEscortFlag(void)
{
    boyInfo[1] |= 0x800000000LL;
}

inline void SetBoyWeaponGObj(void *w)
{
    if (boyGObj != 0 && w != 0) {
        SwapBoyWeapon(0, w, boyGObj);
    }
}

inline int IsBoyStatus_NotDanger(void)
{
    unsigned int st = (unsigned int)GOBJ_ACT(boyGObj)->actMode;
    if (st < 0x17) {
        if (st >= 0x14) {
            return 1;
        }
    }
    return 0 < GOBJ_WORK(boyGObj)->downTimer;
}

inline int GetEfStageCameraTargetID(void)
{
    if (BOYEFSTAGE[0]) {
        return *(int *)(BOYEFSTAGE + 4);
    }
    return 0;
}

inline int IsBackFromEfStage(void)
{
    return BOYEFSTAGE[0];
}

inline int PrivInsCamChk(void)
{
    return privInsCam.on != 0;
}

inline unsigned char PrivInsCamChk_Control(void)
{
    return privInsCam.control;
}

inline int *GetbufpCharacterPacket(void)
{
    return characterPacket;
}

inline int GetsizeCharacterPacket(void)
{
    return 32;
}

inline void MakeCharacterPacket(void)
{
    char *pkt = (char *)characterPacket;
    Act *sub;
    char *g;

    *(BoyKidnapWork *)characterPacket = characterPacketDefault;
    if (boyGObj != 0) {
        sub = GOBJ_ACT(boyGObj);
        if (sub->weapon != 0) {
            *(int *)(pkt + 0x8) = sub->weapon->labelId;
        }
        if (sub->curItem != 0) {
            *(int *)(pkt + 0xC) = sub->curItem->labelId;
        }
        if (sub->actMode == 0x2D) {
            *(int *)(pkt + 0x10) = ((GObj *)sub->sofa)->labelId;
        }
        g = (char *)girlGObj;
        if (g != 0 && GOBJ_ACT(g)->actMode == 0x2D) {
            *(int *)(pkt + 0x1C) |= 1;
        }
        BoyInfoUpdate_StageChange();
        *(int *)(pkt + 0x1C) =
            (*(int *)(pkt + 0x1C) & ~0x10000) |
            (((int)((unsigned char)((unsigned long long)boyInfo[1] >> 34)) & 1) << 16);
        *(unsigned char *)(pkt + 0x1D) =
            (int)((unsigned char)((unsigned long long)boyInfo[1] >> 33)) & 1;
        *(CharPos *)pkt = BOYINFO.f50;
    }
}

inline void BoyInfoUpdate_StageChange(void)
{
    GObj *g = boyGObj;
    Act *sub = GOBJ_ACT(g);
    GObj *w;
    ICO_WORD_PTR(GObj *) x;

    BOYINFO.torch = 0;
    w = sub->weapon;
    if (w != 0) {
        x = ACTGame_isWeaponEnableCatchfire(w);
        if (x != 0) {
            if (IsTorchLightOn((GObj *)x)) {
                BOYINFO.torch = 1;
            }
        }
    }
    BOYINFO.fire = 0;
    if (ACTGame_FLAG_TETSUNAGI()) {
        BOYINFO.fire = 1;
    }
}

typedef struct { /* field names derived */
    CharPos pos; /* 0x00 */
    int boyID;   /* 0x08 */
    int girlID;  /* 0x0C */
    int f10;     /* 0x10 */
    int f14;
    int f18;
    unsigned int b1C : 8; /* 0x1C */
    unsigned int b1D : 8;
    unsigned int h1E : 16; /* 0x1E */
} CharacterPacket;         /* derived name */

inline void ReadCharacterPacket(void)
{
    CharacterPacket *p = (CharacterPacket *)characterPacket;

    BOYINFO.boyID = p->boyID;
    BOYINFO.girlID = p->girlID;
    BOYINFO.layoutID = p->f10;
    BOYINFO.bit32 = p->b1C;
    BOYINFO.fire = p->b1D;
    BOYINFO.torch = p->h1E;
    BOYINFO.f50 = p->pos;
}

inline void ACTSearchGObj(void *self, int kind, int maxDeg, ICO_WORD *out_id, float *out_vec,
                          float thresh)
{
    searchGObj(self, kind, maxDeg, out_id, out_vec, thresh);
}

inline void afterBoySwim(GObj *volatile self)
{
    RequestChangeHandMode(self, 0, 3, 0, 0, 0, 0);
    GOBJ_SUB(self)->root.filter = InitialColInfo;
    debug_StdPrintfDummy("after");
}

static int boyActUnusedWord = -1; /* derived name */

inline void actBoyJump(GObj *volatile self)
{
    while (1) {
        ACTSendMailCorrect(self, 0xBD);
        _ACTWait(1);
    }
}

inline void afterBoyTakeWeapon(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    SwapBoyWeapon(BOYINFO.weapon, BOYINFO.nextWeapon, (void *)self);
    sub->weapon = BOYINFO.nextWeapon;
}

inline void afterBoyHangG3M(int x)
{
    volatile int local = x;
}

inline void afterBoyRescueGirlBhang(GObj *volatile self)
{
    ACTGame_DisconnectHand();
}

inline void subBoyBrainMain(int arg)
{
    volatile int local = arg;
    while (1) {
        _ACTWait(1);
    }
}

inline void SetBoyInfo(GObj *weapon, GObj *item)
{
    int n;
    int i;
    if (weapon != 0) {
        ((int *)boyInfo)[0] = weapon->labelId;
    } else {
        ((int *)boyInfo)[0] = 0;
    }
    i = 0;
    n = 1;
    if (item != (GObj *)(ICO_WORD)i) {
        ((int *)boyInfo)[n] = item->labelId;
    } else {
        ((int *)boyInfo)[n] = i;
    }
}

inline void GetBoyRootPositionForCamera(float *out, GObj *gobj)
{
    float buf[4];
    GObj *g = boyGObj;
    Act *sub;

    sub = GOBJ_ACT(g);
    GetRootPosition(buf, g);
    if (_DistSqGV(buf, (char *)&sub->camRootX) < 40000.0f) {
        out[0] = sub->camRootX;
        out[1] = sub->camRootY;
        out[2] = sub->camRootZ;
    } else {
        GetRootPosition((char *)&sub->camRootX, g);
        out[0] = buf[0];
        out[1] = buf[1];
        out[2] = buf[2];
    }
}

inline void Boy_Init(void)
{
    *(BoyWork *)boyInfo = boyInfoDefault;
    *(BoyKidnapWork *)characterPacket = characterPacketDefault;
}
