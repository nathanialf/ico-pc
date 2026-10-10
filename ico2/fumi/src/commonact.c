#include "ee_view.h"
#include "enemy.h"
#include "enemy_act.h"
#include "isys.h"
#include <assert.h>
#include "camera-editor.h"
#include "ebrain.h"

/* prototypes: their order is the inline tail's emission order (gcc emits every
   inline at the end of the object in first-declaration order). They precede
   commonact.h, whose alphabetical list would otherwise fix that order. */
void ACTAcceptMail(GObj *self, int mail);
inline int _ACTMotDirSmzDirect(GObj *self, float *dir);
void WithMailFunc_Idling(GObj *self);
void WithMailFunc_BossDamaged(GObj *self);
void WithMailFunc_FallDead(GObj *self);
void actCommonRevive(GObj *volatile self);
void actCommonReviveAir(GObj *volatile self);
void actCommonPlay(GObj *volatile self);
void actCommonOne(GObj *volatile self);
void actCommonDelete(GObj *volatile self);
void actCommonCatchFire(GObj *volatile self);
void actCommonCatchFireBomb(GObj *volatile self);
void actCommonPutFire(GObj *volatile self);
void actCommonBoxReverbe(GObj *volatile self);
void actCommonItem(GObj *volatile self);
void actCommonClimb(GObj *volatile self);
void actCommonCliffdown(GObj *volatile self);
void actCommonLadderBellow(GObj *volatile self);
void actCommonLadderBellowHang(GObj *volatile self);
void actCommonEdge(GObj *volatile self);
void actCommonDodge(GObj *volatile self);
void actCommonDodgeJump(GObj *volatile self);
void actCommonGuard(GObj *volatile self);
void actCommonFallDamage(GObj *volatile self);
void actCommonDamage(GObj *volatile self);
void actCommonShoal(GObj *volatile self);
void actCommonSwim(GObj *volatile self);
void actCommonLever2(GObj *volatile self);
void actCommonRopeTouchWall(GObj *volatile self);
void actCommonRopeSwing(GObj *volatile self);
void actCommonRopeTurn(GObj *volatile self);
void actCommonRopeDownEnd(GObj *volatile self);
void actCommonRopeJump(GObj *volatile self);
void actCommonRopeJumpBefore(GObj *volatile self);
void actCommonRopeTurnSpecial(GObj *volatile self);
void actCommonRopeClimbEnd2(GObj *volatile self);
void actCommonCornered(GObj *volatile self);
void actCommonLookaround(GObj *volatile self);
void actCommonTurnWarn(GObj *volatile self);
void actCommonTurnStrict(GObj *volatile self);
void actCommonPPipe(GObj *volatile self);
void actCommonHandrail(GObj *volatile self);
void actCommonOneWall(GObj *volatile self);
void motCommonNull(GObj *volatile self);
void motCommonBoxPush(GObj *volatile self);
void motCommonBoxPull(GObj *volatile self);
void motCommonBarPush(GObj *volatile self);
void motCommonBarPull(GObj *volatile self);
void motCommonLadderUp(GObj *volatile self);
void motCommonLadderDown(GObj *volatile self);
void motCommonSlip(GObj *volatile self);
void motCommonRopejumpDircorrect(GObj *volatile self);
void motCommonHangNone(GObj *volatile self);
void motCommonHangWall(GObj *volatile self);
void motCommonHangCliff(GObj *volatile self);
void motCommonRopeTurnSpecialR(GObj *volatile self);
void motCommonRopeTurnSpecialL(GObj *volatile self);
void motCommonTruckLeverLoop(GObj *volatile self);
void motCommonTruckLeverPull(GObj *volatile self);
void motCommonTruckLeverPush(GObj *volatile self);
void funcCommonRopeBefore(GObj *self, int mail, GObj *chain);
void afterCommonRope(GObj *volatile self);
void extraCommonNull(GObj *volatile self);
void extraCommonCall(GObj *volatile self);
void funcCommonWayOn(GObj *self);
void funcCommonSofaWakeup(GObj *self);
int _ACTMotReqResult(GObj *self, int mot);
inline float *test_CURRENTORIENT(GObj *self);
inline float *test_CURRENTROOT(GObj *self);
void StartCorrectPosition(GObj *self, float *pos, float *dir, int mode, float t);
int IsCorrectPosition(GObj *self);
void ControlMotionOrient(int id, int mot);
int FloorIsTruck(GObj *self);
void _ACTMotDir_V(void *self, void *dir);
void ACTMotDirToWall(GObj *self);
void SetCorrectOrientOfChain(void *self);
void actAfterForceRope(GObj *volatile self);
inline void actAfterForceRopeSwing(GObj *volatile self);
static inline void actAfterRopeJump(GObj *volatile self);
static inline void afterCommonRopeCliff(char *self);
static inline void afterCommonRopeTurnSpecial(GObj *volatile self);
inline void actAfterDown(GObj *volatile self);
void afterCommonCling(ICO_WORD_PTR(GObj *) volatile self);
void actAfterSlip(int x);
inline void afterCommonRevive(ICO_WORD_PTR(GObj *) volatile self);
inline void afterCommonStone(GObj *volatile self);
inline void afterCommonBox(GObj *volatile self);
void afterCommonBar(GObj *volatile self);
static inline void actAfterJump(GObj *volatile self);
inline void actAfterFall(GObj *volatile self);
inline void actAfterFly(GObj *volatile self);
static inline void ClipCollisionWithField(ClipWork *work);
inline void afterCommonOneWall(int x);
int ACTCheckFlagAttack(GObj *self);
static inline void afterCommonBecarry(GObj *volatile self);
inline void afterCommonTruckLever(GObj *volatile self);

#include "commonact.h"
#include "debug.h"
#include "gobj.h"
#include "obj_manager.h"
#include "act-env.h"
#include "act.h"
#include "boyact.h"
#include "girl_act.h"
#include "brain.h"
#include "mail-add-data.h"
#include "gflag.h"
#include "Matrix.h"
#include "Primitive.h"
#include "boy.h"
#include "cage.h"
#include "darkVolume.h"
#include "lineManager.h"
#include "motionOrientManager.h"
#include "rotObject.h"
#include "torch.h"
#include <libvu0.h>
#include "geometryManager.h"
#include "typedef.h"
#include "act-game.h"
#include "matrixDrive.h"
#include "sugiCommon.h"
#include "layout_action.h"
#include "script.h"
#include "motionFileManager.h"
#include "GifPacket.h"
#include "st25a.h"
#include <string.h>
#include "gamesys.h"
#include "ico_gamestate.h" /* port: achievement signals */
#include "generator.h"
#include "debug_exception.h"
#include "gv.h"
#include "box.h"
#include "quaternion.h"
#include "main.h"
#include "fieldCollision.h"
#include "chain.h"
#include "motionManager2.h"
#include "weapon.h"
#include "act-way.h"
#include "pad.h"
#include "s_init.h"
#include "clipCollisionManager.h"
#include "layout_texture.h"
#include "flyManager.h"

/* PC port: the climb mail hands the enemy's climb wall over as an orient
   target, copying a MotOriTarget out of ClimbCol (wallSrc, wall: the
   target's WallCfg); the host layouts must agree (tools/template_audit.py) */
ICO_LAYOUT_SIZE(MotOriTarget, ClimbCol);

ICO_LAYOUT_AT(MotOriTarget, wall.o, ClimbCol, wallSrc);

ICO_LAYOUT_AT(MotOriTarget, wall.elem, ClimbCol, wall);

static void DamageFunc(GObj *self);
static void TestCageUpDown(GObj *cage, GObj *gobj);

typedef struct { /* field names derived */
    int a, b, c;
} Blob12; /* derived name */

typedef struct { /* field names derived */
    int w[6];
} SlowrunRec; /* derived name */

void ACTSetPositionWithFitting(void *self, float *pos)
{
    SetDirectRootPosition(self, pos);
}

static void ACTSetPositionNoFitting(void *self, float *pos)
{
    SetDirectRootPositionNoFitting(self, pos);
}

void ACTSetPositionNodeWithFitting(void *self, int node, float *pos, float t)
{
    SetDirectRootPositionWithNodePoint(self, node, pos, t);
}

static int ChangeMailInLadder(GObj *self, int mail)
{
    float p[4];
    float q[4];
    Act *s = GOBJ_ACT(self);
    int ret = mail;
    int flag = 0;
    float lim1;
    float lim2;

    if (boyGObj != 0 && girlGObj != 0) {
        if (self == boyGObj) {
            if (s->actMode == 38) {
                flag = (GOBJ_ACT(girlGObj)->actMode == 38);
            }
        }
        if (self == girlGObj) {
            if (s->actMode == 38) {
                flag = 1;
            }
        }
        if (flag != 0 &&
            _DistxzSqGV(test_CURRENTROOT(boyGObj), test_CURRENTROOT(girlGObj)) < 3600.0f) {
            lim1 = self == boyGObj ? 175.0f : 225.0f;
            lim2 = self == boyGObj ? 175.0f : 210.0f;
            if (self == boyGObj) {
                p[0] = test_CURRENTROOT(self)[0];
                p[1] = test_CURRENTROOT(boyGObj)[1];
                p[2] = test_CURRENTROOT(boyGObj)[2];
                q[0] = test_CURRENTROOT(girlGObj)[0];
                q[1] = test_CURRENTROOT(girlGObj)[1];
                q[2] = test_CURRENTROOT(girlGObj)[2];
            } else {
                p[0] = test_CURRENTROOT(girlGObj)[0];
                p[1] = test_CURRENTROOT(girlGObj)[1];
                p[2] = test_CURRENTROOT(girlGObj)[2];
                q[0] = test_CURRENTROOT(boyGObj)[0];
                q[1] = test_CURRENTROOT(boyGObj)[1];
                q[2] = test_CURRENTROOT(boyGObj)[2];
            }
            switch (mail) {
            case 0x14A:
                if (q[1] < p[1] && (q[1] - p[1] < 0.0f ? -(q[1] - p[1]) : q[1] - p[1]) < lim2) {
                    s->flags18.ll |= (1ULL << 36);
                    ret = 0x150;
                }
                break;
            case 0x14B:
                if (q[1] > p[1] && (q[1] - p[1] < 0.0f ? -(q[1] - p[1]) : q[1] - p[1]) < lim1) {
                    s->flags18.ll |= (1ULL << 37);
                    ret = 0x150;
                }
                break;
            }
        }
    }
    return ret;
}

/* motionOrientManager.h declares none of the motion tables */
extern MotionDef motionKind[];

/* a static inline used only by _ACTCorrectMsg */
static inline int GetHitDirIdx(GObj *self) /* derived name */
{
    float *p = GOBJ_ACT(self)->attackDir;
    int a = _RotyGV(test_CURRENTORIENT(self), p);

    if (-45 <= a && a <= 45) {
        return 0;
    }
    if (a < -134 || 134 < a) {
        return 1;
    }
    if (46 <= a && a <= 134) {
        return 2;
    }
    if (-134 <= a) {
        if (a <= -46) {
            return 3;
        }
    }
    return 0;
}

int _ACTCorrectMsg(GObj *self, int msg, void *param)
{
    float pos[4];
    Act *sk = GOBJ_ACT(self);
    int fast = _ACTGame_GetParamF(2) < GOBJ_SUB(self)->ctrl.groundHeight;

    switch (msg) {
    case 309:
        if (!(((int)(sk->flags18.ll >> 53) & 1) &&
              GOBJ_WORK(self)->fallDamageHeight < GOBJ_SUB(self)->ctrl.groundHeight)) {
            msg = 418;
        }
        break;
    case 42:
        if (!(((int)(sk->flags18.ll >> 53) & 1) &&
              GOBJ_WORK(self)->fallDamageHeight < GOBJ_SUB(self)->ctrl.groundHeight)) {
            msg = 418;
        }
        break;
    case 259:
        if (((motionKind + GOBJ_SUB(self)->ctrl.motion)->flags.word >> 9) & 1) {
            msg = 418;
        }
        break;
    case 298:
        if (sk->actMode == 5 && ((int *)GOBJ_WORK(self)->modeHist)[0] == 26) {
            msg = 418;
        }
        break;
    case 297:
        if (sk->actMode == 1 && ((long long *)GOBJ_WORK(self)->modeHist)[0] == 0x1A00000005LL) {
            msg = 418;
        }
        break;
    case 330:
    case 331:
        msg = ChangeMailInLadder(self, msg);
        break;
    case 240:
        if (((motionKind + GOBJ_SUB(self)->ctrl.motion)->flags.word >> 5) & 1) {
            msg = 418;
        } else if ((int)(sk->flags20.ll >> 14) & 1) {
            msg = 239;
        }
        break;
    case 241:
        if (((motionKind + GOBJ_SUB(self)->ctrl.motion)->flags.word >> 5) & 1) {
            msg = 418;
        }
        break;
    case 231:
        if (GOBJ_WORK(self)->turnTimer != 0) {
            msg = 418;
        } else if ((int)(sk->flags20.ll >> 14) & 1) {
            msg = 233;
        }
        break;
    case 232:
        if (GOBJ_WORK(self)->turnTimer2 != 0) {
            msg = 418;
        } else if ((int)(sk->flags20.ll >> 14) & 1) {
            msg = 234;
        }
        break;
    case 348:
    case 349:
    case 350:
        if (gameover_flag != 0) {
            msg = 418;
        }
        break;
    case 205:
        if (sk->msgBlockTimer != 0) {
            msg = 418;
        }
        break;
    case 174:
        if (fast) {
            msg = 418;
        }
        if (((int)(sk->flags18.ll >> 53) & 1) &&
            GOBJ_SUB(self)->ctrl.groundHeight < GOBJ_WORK(self)->fallDamageHeight) {
            msg = 418;
        }
        break;
    case 26:
        if (GOBJ_SUB(self)->ctrl.waterDepth < (self == boyGObj ? 110.0f : 135.0f)) {
            msg = 418;
        }
        /* fallthrough */
    case 218:
        if (self->kind == 4) {
            msg = 223;
            if ((int)(sk->flags20.ll >> 30) & 1) {
                msg = 418;
            }
        }
        break;
    case 219:
        if (self->kind == 4) {
            msg = 223;
            if ((int)(sk->flags20.ll >> 30) & 1) {
                msg = 418;
                if (13 <= sk->frame) {
                    msg = 30;
                    iosOmSendMail(self, 29, param);
                }
            }
        }
        break;
    case 21:
        if (sk->actMode == 62 && param == sk->lastChain) {
            msg = 418;
        }
        if (fast) {
            msg = 418;
        }
        if (IsAbleChainHang(param) == 0) {
            msg = 418;
        }
        if (((int)(sk->flags18.ll >> 53) & 1) &&
            GOBJ_SUB(self)->ctrl.groundHeight < GOBJ_WORK(self)->fallDamageHeight) {
            msg = 418;
        }
        break;
    case 20:
        if (IsAbleChainHang(param) == 0) {
            msg = 418;
        }
        if (((int)(sk->flags18.ll >> 53) & 1) &&
            GOBJ_SUB(self)->ctrl.groundHeight < GOBJ_WORK(self)->fallDamageHeight) {
            msg = 418;
        }
        break;
    case 7:
        if (((motionKind + GOBJ_SUB(self)->ctrl.motion)->modeBits.word >> 19) & 7) {
            if (sk->heldItem.i != 0) {
                msg = 315;
            }
        }
        break;
    case 10: {
        int iv = ps2_ftoi(GOBJ_SUB(self)->ctrl.fallHeight); /* EE cvt.w.s saturates */
        int flagA = 0;
        int flagB = 0;

        if (120.0f < GOBJ_SUB(self)->ctrl.fallHeight) {
            ACTWay_SetBeginPositionIllegal(self);
            /* disabled in retail: the way-begin-position (WBP) report of the
               landing */
            if (0) {
                debug_StdPrintfDummy("WBP set [landing]\n");
            }
        }
        if (!(((motionKind + GOBJ_SUB(self)->ctrl.motion)->flags.word >> 25) & 1)) {
            msg = 418;
            break;
        }
        if (iv < 130) {
            iosOmSendMail(self, 59, param);
        }
        if (self == girlGObj && ACTGame_FLAG_TETSUNAGI()) {
            iosOmSendMail(self, 58, param);
            if (iv < 130) {
                iosOmSendMail(self, 57, param);
            }
        }
        if (((int *)GOBJ_WORK(self)->modeHist)[0] == 38) {
            iosOmSendMail(self, 60, self);
        }
        if (_ACTGame_GetParamF(2) < (float)iv) {
            flagA = 1;
        } else if (_ACTGame_GetParamF(1) < (float)iv) {
            flagB = 1;
        }
        switch (sk->actMode) {
        case 95:
            flagB = 1;
            break;
        case 96:
            flagA = 1;
            break;
        }
        if (flagA) {
            msg = 44;
        } else if (flagB) {
            msg = 43;
        }
        if (((int *)GOBJ_WORK(self)->modeHist)[0] == 21) {
            iosOmSendMail(self, 56, param);
            if (msg == 43) {
                iosOmSendMail(self, 55, param);
            }
        }
        break;
    }
    case 14:
        debug_StdPrintfDummy("critical hit to boss!!!");
        if (GOBJ_ACT(self)->enemy->bossLife <= 1) {
            msg = 293;
        } else {
            msg = 285;
        }
        break;
    case 13: {
        float d = sk->life - (float)GOBJ_ACT(self)->damage;

        if (self == boyGObj || self == girlGObj) {
            d = sk->life = 100.0f;
        }
        if (GOBJ_ACT(self)->unguardable != 0) {
            debug_StdPrintfDummy("!!! unable guard flag get\n");
        }
        if (stage_no == 85 || debug_use_new_queen_battle != 0) {
            if (GOBJ_ACT(self)->stoneHit != 0) {
                if (ACTGame_NoWeapon(self) != 0 && sk->actMode != 14) {
                    msg = 110;
                    GOBJ_ACT(self)->enemy->liftLevel = 10;
                    BoySekikaTexScroll();
                    GOBJ_ACT(self)->enemy->stoneHitNoWeapon += 1;
                    break;
                } else {
                    msg = 282;
                    GOBJ_ACT(self)->enemy->stoneHitWeapon += 1;
                    GOBJ_ACT(self)->enemy->liftLevel = 10;
                    break;
                }
            }
        }
        if (_AbsRotyGV(test_CURRENTORIENT(self), GOBJ_ACT(self)->attackDir) < 60 &&
            sk->actMode != 15 && sk->actMode != 20 && GOBJ_ACT(self)->unguardable == 0 &&
            !((int)(sk->flags18.ll >> 51) & 1)) {
            iosOmSendMail(self, 283, param);
            debug_StdPrintfDummy("guard mail\n");
        } else {
            debug_StdPrintfDummy("guard error=[%d][%d][%d][%d]\n",
                                 _AbsRotyGV(test_CURRENTORIENT(self), GOBJ_ACT(self)->attackDir),
                                 sk->actMode, 15, 20);
        }
        if (GOBJ_ACT(self)->stoneHit != 0) {
            GOBJ_ACT(self)->enemy->stoneLevel += 1;
            GOBJ_ACT(self)->enemy->liftLevel = 10;
        }
        if (self->kind == 4 && GOBJ_ACT(self)->enemy->liftKind == 3) {
            if (GOBJ_ACT(self)->enemy->slowTimer <= 0) {
                DamageFunc(self);
                iosPadActRequest(boyPad, 2);
                pos[0] = test_CURRENTROOT(self)[0];
                pos[1] = test_CURRENTROOT(self)[1];
                pos[2] = test_CURRENTROOT(self)[2];
                soundSeDefPlay(382, 0, pos, 1);
            }
            msg = 418;
            GOBJ_ACT(self)->enemy->slowTimer = 60;
            break;
        }
        if (self == boyGObj && ((struct GObj *)param)->kind == 17) {
            msg = 418;
            break;
        }
        if (self->kind == 4 && sk->actMode == 16 &&
            (char *)GOBJ_ACT(self)->enemy->clingTarget == (char *)girlGObj) {
            msg = 215;
            break;
        }
        if (self->kind == 4 && GOBJ_ACT(self)->enemy->liftKind != 3 &&
            EnemyGetNSafeParts(self) < 8) {
            msg = 293;
            debug_StdPrintfDummy("die!!!!!!!!!!!\n");
            break;
        }
        if (d < 0.0f) {
            msg = 293;
            if (self == boyGObj) {
                msg = 291;
                sk->life = 100.0f;
            }
            debug_StdPrintfDummy("die!!!!!!!!!!!\n");
            break;
        }
        if (GOBJ_ACT(self)->downHit != 0 || _ACTCharStatus_Check(self, 16) != 0) {
            msg = 291;
            debug_StdPrintfDummy("down!!!!!!!!!!!\n");
            break;
        }
        {
            int dir = GetHitDirIdx(self);

            msg = dir + 287;
            debug_StdPrintfDummy("damage!!!!!!!!!!!  %d\n", dir);
        }
        break;
    }
    }
    return msg;
}

/* the motion each interrupt kind requests, indexed by the kind */
static int intrMotion[432] = {
    0,   0,   0,   0,   0,   0,   117, 117, 61,  0,   1,   0,   0,   0,   0,   222, 223, 0,   0,
    0,   84,  85,  0,   164, 119, 168, 172, 1,   20,  22,  23,  0,   0,   0,   136, 0,   -1,  -1,
    148, 270, -1,  126, 125, 165, 166, 0,   0,   0,   121, 120, 152, 153, 154, 266, 267, 149, 149,
    4,   1,   4,   5,   0,   0,   0,   0,   63,  64,  65,  0,   0,   0,   1,   0,   1,   0,   114,
    13,  8,   1,   105, 1,   0,   104, 115, 116, 16,  14,  1,   112, 113, 94,  101, 1,   0,   0,
    0,   1,   1,   0,   0,   0,   1,   1,   0,   0,   0,   1,   0,   0,   21,  136, 137, 138, 0,
    68,  69,  70,  1,   87,  88,  87,  167, 167, 71,  72,  73,  74,  75,  77,  76,  78,  79,  79,
    81,  80,  127, 128, 129, 130, 126, 123, 124, 131, 117, 126, 155, 155, 85,  203, 204, 205, 206,
    207, 158, 157, 129, 130, 208, 210, 209, 212, 211, 201, 164, 202, 213, 214, 215, 83,  82,  0,
    0,   164, 0,   84,  213, 207, 216, 0,   6,   7,   8,   9,   10,  14,  15,  13,  16,  19,  24,
    25,  27,  27,  26,  28,  29,  30,  186, 1,   1,   2,   40,  41,  12,  1,   43,  43,  44,  51,
    50,  50,  52,  49,  48,  118, 148, 174, 8,   0,   172, 1,   173, 174, 166, 57,  58,  118, 200,
    89,  260, 53,  223, 222, 225, 224, 225, 224, 223, 222, 13,  8,   226, 223, 222, 226, 226, 0,
    66,  67,  1,   13,  1,   0,   261, 118, 0,   0,   0,   270, 270, 263, 31,  270, 11,  12,  11,
    1,   8,   1,   0,   218, 219, 220, 221, 32,  33,  34,  35,  36,  37,  38,  39,  139, 139, 140,
    150, 151, 141, 142, 143, 144, 146, 147, 148, 0,   61,  59,  54,  55,  56,  62,  155, 156, 131,
    169, 170, 61,  61,  132, 133, 134, 135, 61,  117, 117, 122, 118, 94,  95,  96,  95,  97,  98,
    99,  90,  59,  60,  91,  92,  93,  158, 159, 160, 161, 162, 163, 164, 0,   0,   0,   271, 272,
    245, 246, 243, 192, 193, 194, 195, 198, 198, 197, 196, 199, 227, 236, 237, 228, 229, 230, 231,
    232, 233, 229, 240, 8,   1,   241, 247, 248, 248, 249, 250, 251, 252, 253, 255, 256, 175, 176,
    177, 175, 176, 177, 178, 179, 179, 180, 180, 0,   0,   181, 182, 183, 184, 185, 105, 187, 1,
    188, 189, 190, 191, 189, 118, 1,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
    0,   0,   0,   0,   0,   0,   0,   0,   17,  18,  262, 0,   0,   0}; /* derived name */

int ACTGetOrientFromIntrK(GObj *self, int k, MotOriReq *out, int arg)
{
    Act *s = GOBJ_ACT(self);
    MotOriTarget tmp;
    int ret = intrMotion[k];

    *out = s->env.motOriReq;
    switch (k) {
    case 140:
        out->b = GOBJ_WORK(self)->cliffReq.b;
        break;
    case 305:
        out->a = GOBJ_WORK(self)->cliffReq.a;
        break;
    case 298:
        s->intrData = GetMailAdditionalData(self, arg);
        tmp = *(MotOriTarget *)(GOBJ_ACT(self)->intrData);
        out->b = s->env.motOriReq.b = tmp;
        break;
    case 54:
        s->intrData = GetMailAdditionalData(self, arg);
        tmp = *(MotOriTarget *)(GOBJ_ACT(self)->intrData);
        out->a = out->b = s->env.motOriReq.a = s->env.motOriReq.b = tmp;
        s->env.sofaObj.obj = tmp.obj;
        GetSofaPosition(self, s->env.sofaObj.obj);
        break;
    case 145:
        out->a = GOBJ_WORK(self)->bellowWall3000;
        break;
    case 144:
        out->a = GOBJ_WORK(self)->bellowWall400;
        break;
    case 114:
    case 115:
    case 116:
    case 117:
        out->b = s->env.motOriReq.a;
        break;
    case 45:
        return GOBJ_ACT(self)->enemy->jumpOrient;
    case 63:
        if (s->actMode == 74) {
            ret = 272;
        }
        break;
    case 378:
    case 379:
    case 380:
        out->b = out->a = s->env.cliffContact.b;
        break;
    case 381:
        *out = s->env.cliffContact = GOBJ_ACT(boyGObj)->env.cliffContact;
        break;
    case 382:
    case 383:
    case 384:
        *out = s->env.cliffContact;
        break;
    case 385:
    case 386:
        *out = s->env.supportReq;
        s->env.motOriReq = *out;
        break;
    case 205:
    case 206:
    case 207:
        if (self == boyGObj) {
            if (ACTGame_NoWeapon(self)) {
                ret = 42;
            } else {
                ret = ACTGame_GetMotOrientFromWeapon(s->weapon);
            }
        }
        break;
    case 72:
    case 74:
    case 338:
        ret = s->orientMot;
        break;
    case 81:
        ret = s->orientMot;
        out->a = GOBJ_ACT(boyGObj)->env.motOriReq.b;
        s->env.motOriReq = *out;
        break;
    case 89:
    case 399:
    case 403:
        out->a = GOBJ_ACT(boyGObj)->env.motOriReq.b;
        s->env.motOriReq = *out;
        break;
    case 416:
        ret = 0;
        if (s->soundMot != 0 && !((int)(s->flags20.ll >> 13) & 1)) {
            ret = s->soundMot;
            s->soundMot = 0;
        }
        break;
    case 152:
    case 155:
    case 156:
        out->a = *(MotOriTarget *)&GOBJ_ACT(self)->enemy->climbCol;
        break;
    default:
        ret = intrMotion[k];
        break;
    }
    return ret;
}

static inline void setIntrFlags(IntrMail *intr) /* derived name */
{
    IntrMail *p;

    for (p = intr; p != 0 && (short)p->kind != 429; p++) {
        p->flags |= 0x40000;
    }
}

static inline void correctIntrList(IntrMail *intr, IntrMail *corr) /* derived name */
{
    IntrMail *ip;
    IntrMail *q;

    for (q = corr; q != 0 && (short)q->kind != 429; q++) {
        if (q->mode == -1) {
            for (ip = intr; ip != 0 && (short)ip->kind != 429; ip++) {
                if (ip->kind == q->kind) {
                    ip->flags &= ~0x40000;
                    debug_StdPrintfDummy("off!!\n");
                }
            }
        }
    }
}

void ACTRunIntrCorrect(GObj *self, IntrMail *intr, IntrMail *corr)
{
    char *rec;
    Act *s = GOBJ_ACT(self);

    setIntrFlags(intr);
    correctIntrList(intr, corr);
    rec = (char *)&motionKind[GOBJ_SUB(self)->ctrl.motion];
    if (rec[399] & 1) {
        actIntrList[10].flags |= 0x40000;
    } else {
        actIntrList[10].flags &= ~0x40000;
    }
    if (s->actMode == 26) {
        if (s->modeFrame < ((60 - systemStatus[0] * 10) / systemStatus[1]) / 3) {
            actIntrList[371].flags |= 0x40000;
        } else {
            actIntrList[371].flags &= ~0x40000;
        }
    }
}

void WithMailFunc_WayBeginPosError(void *self)
{
    /* disabled in retail: the way-begin-position (WBP) report */
    if (0) {
        debug_StdPrintfDummy("WBP set [with mail]\n");
    }
    ACTWay_SetBeginPositionIllegal(self);
}

void WithMailFunc_AttackFail(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    char *p = (char *)s->intrData;
    int v = p != 0 ? *(int *)p : s->env.wallWord;
    if (self == boyGObj) {
        GObj *t = s->weapon;
        if (t != 0) {
            GOBJ_SUB(t)->ctrl.wallAttr = v;
            ExecWeaponHitReaction(t);
        }
    }
}

void WithMailFunc_AttackRejectInQueen(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    GObj *w = s->weapon;
    if (w != 0) {
        ReleaseWeaponWithFumbleSequential(w);
        s->weapon = 0;
    }
    if (stage_no == 85 || debug_use_new_queen_battle != 0) {
        void *e = isysGObjSearchFromObjKindID_begin(54);
        if (e != 0) {
            iosOmSendMail(e, 0xD, self);
        }
    }
}

void GetCorrectOrientOfChain(void *buf, void *obj)
{
    float q[4];
    int deg;

    if (GetChainDirCorrectVal(GOBJ_ACT(obj)->chain, &deg) != 0) {
        memset(q, 0, 16);
        q[2] = 1.0f;
        _ApplyRyGV(q, (float)RoundDegGV(
                          deg + AlignDegGV(RoundDegGV((int)(_GetDirection(test_CURRENTORIENT(obj)) /
                                                            3.1415927f * 180.0f) -
                                                      deg))) *
                          3.1415927f / 180.0f);
        ((float *)buf)[0] = q[0];
        ((float *)buf)[1] = q[1];
        ((float *)buf)[2] = q[2];
    } else {
        ((float *)buf)[0] = *(float *)((char *)test_CURRENTORIENT(obj) + 0);
        ((float *)buf)[1] = *(float *)((char *)test_CURRENTORIENT(obj) + 4);
        ((float *)buf)[2] = *(float *)((char *)test_CURRENTORIENT(obj) + 8);
    }
}

/* whether p is a box (kind 0x11), as a char */
static inline char ropeWallIsBox(char *p) /* derived name */
{
    if (p != 0 && ((struct GObj *)p)->kind == 0x11) {
        return 1;
    }
    return 0;
}

static int CollisCheckInRope(void *self, GObj *chain)
{
    ClipWork work;
    float mid[4];
    float p[4];
    float dir[4];
    float tmp[4];
    float n51[4];
    float n47[4];
    int rv = 0;

    sceVu0ScaleVector(dir, test_CURRENTORIENT(self), 15.0f);
    GetSkeltonPosition(n51, self, 51);
    GetSkeltonPosition(n47, self, 47);
    sceVu0AddVector(mid, n51, n47);
    sceVu0ScaleVector(mid, mid, 0.5f);
    p[0] = mid[0];
    p[2] = mid[2];
    p[1] = mid[1] + 10.0f;
    sceVu0ScaleVector(tmp, dir, -1.0f);
    sceVu0AddVector(work.pt[0], p, tmp);
    sceVu0ScaleVector(tmp, dir, 1.0f);
    sceVu0AddVector(work.pt[1], p, tmp);
    work.radius = 10.0f;
    ClipWall(&work);
    if (work.wall.elem != 0) {
        if (ropeWallIsBox((char *)work.wall.o.obj))
            rv = 2;
        else
            rv = 1;
    } else {
        SwapGV(work.pt[0], work.pt[1]);
        ClipWall(&work);
        if (work.wall.elem != 0) {
            if (ropeWallIsBox((char *)work.wall.o.obj))
                rv = 2;
            else
                rv = 1;
        }
    }
    return rv;
}

static inline int chainFloorHit(GObj *self, ClipWork *w) /* derived name */
{
    if (((motionKind + GOBJ_SUB(self)->ctrl.motion)->flags.word >> 4) & 1) {
        GetSkeltonPosition(w->pt[0], self, 44);
        GetSkeltonPosition(w->pt[1], self, 51);
        w->pt[1][1] -= 5.0f;
        ClipFloor(w);
        if (w->floor.elem != 0) {
            return 1;
        }
    }
    return 0;
}

inline void afterCommonRope(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    debug_StdPrintfDummy("common rope after func\n");
    ReleaseChain(s->chain, self);
    {
        GObj *g = self;
        ICO_RAW(int, (int)s, 0x194, s->lastChain) = s->chain;
        GOBJ_SUB(g)->root.ropeState = 0;
    }
}

inline void actAfterForceRope(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    if (s->chain == 0) {
        debug_assert("src/commonact.c", 1531);
        __assert("src/commonact.c", 1531, "ROPE_GOBJ!=NULL");
    }
    UnLockChainGeo(s->chain);
}

void actCommonRope(GObj *volatile self)
{
    Act *s;
    float dir[4];
    float ori[4];
    float hand[4];
    ClipWork work;
    int step;
    int nsteps;
    int total;
    int roty;

    nsteps = ((60 - systemStatus[0] * 10) / systemStatus[1]) / 3;
    total = nsteps;
    step = -1;
    s = GOBJ_ACT(self);
    GetCorrectOrientOfChain(dir, (void *)self);
    ori[0] = *(float *)((char *)test_CURRENTORIENT(self) + 0);
    ori[1] = *(float *)((char *)test_CURRENTORIENT(self) + 4);
    ori[2] = *(float *)((char *)test_CURRENTORIENT(self) + 8);
    roty = _RotyGV(dir, ori);
    if (s->after == 0) {
        GetRootPositionHandExtra(self, hand);
        step = 0;
        HoldChain(s->chain, self, hand);
    }
    s->after = afterCommonRope;
    ACT_AFTER_PROC(s) = (void (*)(GObj *))actAfterForceRope;
    LockChainGeo(s->chain);
    _ACTWait(1);
    debug_StdPrintfDummy("enter actCommonRope\n");
    while (1) {
        UnLockChainGeo(s->chain);
        ChainGeo(s->chain);
        LockChainGeo(s->chain);
        switch (CollisCheckInRope((void *)self, s->chain)) {
        case 1:
            ACTSendMailCorrect(self, 0xA8);
            break;
        case 2:
            ACTSendMailCorrect(self, 0xA7);
            break;
        }
        if (chainFloorHit(self, &work)) {
            ACTSendMailCorrect(self, 0xA9);
        }
        if (step >= 0) {
            if (step == nsteps) {
                SetMotionDirection(self, dir);
            } else if (step < nsteps) {
                work.pt[0][0] = ori[0];
                work.pt[0][1] = ori[1];
                work.pt[0][2] = ori[2];
                _ApplyRyGV(work.pt[0], (float)(roty * step / total) * 3.1415927f / 180.0f);
                SetMotionDirection(self, work.pt[0]);
            }
            step++;
        }
        if (CheckChainClimbablePos(s->chain)) {
            GetChainClimbOrient(GOBJ_ACT(self)->enemy->climbOrient, s->chain);
            GetRootPosition(GOBJ_ACT(self)->enemy->climbPos, (void *)s->chain);
            GetChainClimbCollision(&GOBJ_ACT(self)->enemy->climbCol, s->chain);
            GOBJ_ACT(self)->enemy->climbObj = s->chain;
            ActSendMail_WithAdditionalData(self, 0x98, (void *)self,
                                           (char *)GOBJ_ACT(self)->enemy->climbOrient);
        }
        _ACTWait(1);
    }
}

/* the turn to the chain's corrected orient, which SetCorrectOrientOfChain
   and the rope turns inline */
static inline void setCorrectOrientOfChain(void *self) /* derived name */
{
    float local[4];
    GetCorrectOrientOfChain(local, self);
    SetMotionDirection(self, local);
}

void motCommonRopeTurnR(GObj *volatile self)
{
    int i = 0;
    int base = (int)(_GetDirection(test_CURRENTORIENT(self)) / 3.1415927f * 180.0f);
    float dir[4];
    int wait = 18, deg = 0;

    while (1) {
        i++;
        memset(dir, 0, 16);
        dir[2] = 1.0f;
        _ApplyRyGV(dir, (float)RoundDegGV(base + deg) * 3.1415927f / 180.0f);
        debug_Arrow(200.0f, test_CURRENTROOT((void *)self), dir, 0xFF, 0, 0xFF);
        SetMotionDirection(self, dir);
        deg += 5;
        if (i % wait == 0) {
            setCorrectOrientOfChain((void *)self);
            ACTSendMailCorrect(self, 0x150);
        }
        _ACTWait(1);
    }
}

void motCommonRopeTurnL(GObj *volatile self)
{
    int i = 0;
    int base = (int)(_GetDirection(test_CURRENTORIENT(self)) / 3.1415927f * 180.0f);
    float dir[4];
    int wait = 18, deg = 0;

    while (1) {
        i++;
        memset(dir, 0, 16);
        dir[2] = 1.0f;
        _ApplyRyGV(dir, (float)RoundDegGV(base - deg) * 3.1415927f / 180.0f);
        debug_Arrow(200.0f, test_CURRENTROOT((void *)self), dir, 0xFF, 0, 0xFF);
        SetMotionDirection(self, dir);
        deg += 5;
        if (i % wait == 0) {
            setCorrectOrientOfChain((void *)self);
            ACTSendMailCorrect(self, 0x150);
        }
        _ACTWait(1);
    }
}

typedef union { /* field names derived */
    float f[4];
    long long ll[2];
} ClimbVec4; /* derived name */

typedef struct { /* field names derived */
    sceVu0FVECTOR v0;
    float v1[4];
    ClimbCol climbCol; /* the wall the climb held */
    GObj *obj;         /* the chain or cage climbed */
} ClimbEndRec;         /* derived name */

/* PC port: the climb mail's data is &enemy->climbOrient, read here whole as
   a ClimbEndRec; the host layouts must agree from there, and the vectors are
   copied as quadwords (tools/template_audit.py) */
ICO_LAYOUT_AT_FROM(ClimbEndRec, v1, EnemyBattleWork, climbOrient, climbPos);

ICO_LAYOUT_AT_FROM(ClimbEndRec, climbCol, EnemyBattleWork, climbOrient, climbCol);

ICO_LAYOUT_AT_FROM(ClimbEndRec, obj, EnemyBattleWork, climbOrient, climbObj);

_Static_assert(__builtin_offsetof(EnemyBattleWork, climbOrient) % 16 == 0,
               "EnemyBattleWork.climbOrient is not on a quadword on the host");

void actCommonRopeClimbEnd1(GObj *volatile self)
{
    ClimbEndRec c;
    ClimbVec4 dir;
    float pos[4];
    float ori[4];
    float hand[4];
    float foot[4];
    float base[4];
    int flag;
    int step = 5;
    int isCage;
    int i;
    long long back;
    int n;
    int r;

    c = *(ClimbEndRec *)(char *)GOBJ_ACT(self)->intrData;
    isCage = c.obj->kind == 44;
    flag = 0;
    GOBJ_SUB(boyGObj)->root.ropeState = 0;
    dir.f[0] = c.v0[0];
    dir.f[1] = c.v0[1];
    dir.f[2] = c.v0[2];
    sceVu0ScaleVector(&dir, &dir, -1.0f);
    if (_AbsRotyGV(&dir, test_CURRENTORIENT(self)) < 10) {
        flag = 1;
    } else {
        back = -5;
        while (1) {
            if (isCage) {
                TestCageUpDown(c.obj, (void *)self);
            }
            if (GOBJ_SUB(self)->ctrl.motion == 118) {
                break;
            }
            _ACTWait(1);
        }
        i = 0;
        r = _RotyGV(dir.f, test_CURRENTORIENT(self));
        step = (r > -1) ? step : back;
        n = r / step;
        n = (n < 0) ? -n : n;
        GetSkeltonPosition(pos, self, 22);
        pos[0] = c.v1[0];
        pos[2] = c.v1[2];
        while (i < n) {
            i++;
            ori[0] = *(float *)((char *)test_CURRENTORIENT(self) + 0);
            ori[1] = *(float *)((char *)test_CURRENTORIENT(self) + 4);
            ori[2] = *(float *)((char *)test_CURRENTORIENT(self) + 8);
            _ApplyRyGV(ori, (float)step * 3.1415927f / 180.0f);
            SetMotionDirection(self, ori);
            if (0 < step) {
                ACTSendMailCorrect(self, 0xA0);
            } else {
                ACTSendMailCorrect(self, 0xA1);
            }
            SetChainRootUpdateMode(self, 3, pos);
            _ACTWait(1);
        }
        if (GOBJ_SUB(self)->ctrl.motion != 118) {
            do {
                ACTSendMailCorrect(self, 0x150);
                _ACTWait(1);
            } while (GOBJ_SUB(self)->ctrl.motion != 118);
        }
    }
    SetMotionDirection(self, dir.f);
    while (1) {
        ACTSendMailCorrect(self, 0x99);
        if (flag) {
            ACTSendMailCorrect(self, 0x9A);
        }
        GetSkeltonPosition(hand, self, 22);
        GetSkeltonPosition(foot, self, 6);
        base[0] = c.v1[0];
        base[1] = c.v1[1];
        base[2] = c.v1[2];
        if (isCage) {
            TestCageUpDown(c.obj, (void *)self);
        }
        GOBJ_ACT(self)->enemy->climbCol = c.climbCol;
        if (hand[1] - base[1] < 10.0f) {
            ACTSendMailCorrect(self, 0x9B);
        }
        if (foot[1] - base[1] < 10.0f) {
            ACTSendMailCorrect(self, 0x9C);
        }
        _ACTWait(1);
    }
}

typedef union { /* field names derived */
    char *p;
    float *f;
} CagePtr; /* derived name */

void actCommonRopeCliff(GObj *volatile self)
{
    float dst[4];
    float root[4];
    float cur[4];
    float p[4];
    Act *s = GOBJ_ACT(self);
    int n = ((60 - systemStatus[0] * 10) / systemStatus[1]) / 2;
    float y;
    int i = 0;

    ACT_AFTER_PROC(s) = (void (*)(GObj *))afterCommonRopeCliff;
    ((CagePtr *)&GOBJ_SUB(boyGObj)->root.ropeState)->p = 0;
    dst[0] = GOBJ_ACT(self)->enemy->ropeCliffX;
    dst[1] = GOBJ_ACT(self)->enemy->ropeCliffY;
    dst[2] = GOBJ_ACT(self)->enemy->ropeCliffZ;
    GetRootPosition(root, (void *)self);
    while (1) {
        i++;
        if (i <= n) {
            _InterGV(cur, root, dst, (float)i, (float)(n - i));
            SetDirectRootPositionNoFitting((void *)self, cur);
        }
        if (GOBJ_SUB(self)->ctrl.motion == 118) {
            y = test_CURRENTROOT((void *)self)[1];
            SetChainRootUpdateMode(boyGObj, 3, (float *)&GOBJ_ACT(self)->enemy->frontPosX);
            p[0] = test_CURRENTROOT((void *)self)[0];
            p[1] = test_CURRENTROOT((void *)self)[1];
            p[2] = test_CURRENTROOT((void *)self)[2];
            p[1] = y + 5.0f;
            SetDirectRootPositionNoFitting((void *)self, p);
        }
        _ACTWait(1);
    }
}

typedef struct { /* field names derived */
    float a[4];
    float b[4];
    int cnt;
    int lim;
    int last;
} CageUD; /* derived name */

/* the cage up-down interpolation record TestCageUpDown keeps between frames
   (start, goal, frame count, limit, last motion) */
static CageUD cageUpDown = {
    {0.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 0.0f}, 0, 0, -1}; /* derived name */

static inline void initCage(GObj *o) /* derived name */
{
    int n;

    cageUpDown.cnt = 0;
    cageUpDown.lim = (float)*motionTable[GOBJ_SUB(o)->ctrl.motion];
    n = GetSkeltonFocusNode(o, 35);
    cageUpDown.a[0] =
        *(float *)(ICO_RAW(char *, ((CagePtr *)&o->dobj)->p, 0xC, *(char **)&GOBJ_SUB(o)->nodeMtx) +
                   n * 0x40 + 0x30);
    cageUpDown.a[1] =
        *(float *)(ICO_RAW(char *, ((CagePtr *)&o->dobj)->p, 0xC, *(char **)&GOBJ_SUB(o)->nodeMtx) +
                   n * 0x40 + 0x34);
    cageUpDown.a[2] =
        *(float *)(ICO_RAW(char *, ((CagePtr *)&o->dobj)->p, 0xC, *(char **)&GOBJ_SUB(o)->nodeMtx) +
                   n * 0x40 + 0x38);
    cageUpDown.b[0] = cageUpDown.a[0];
    cageUpDown.b[2] = cageUpDown.a[2];
    cageUpDown.b[1] = cageUpDown.a[1] + 200.0f;
}

static inline void cageMove(GObj *cage, GObj *o, float *dst, float *lo, float *hi, float *res,
                            float x, float y, float z) /* derived name */
{
    dst[0] = x;
    dst[1] = y;
    dst[2] = z;
    GetCageChainPoint(lo, hi, cage);
    _InterGV(dst, lo, hi, dst[1] - lo[1], hi[1] - dst[1]);
    sceVu0ScaleVector(res, test_CURRENTORIENT(o), -20.0f);
    sceVu0AddVector(res, dst, res);
    ACTSetPositionNodeWithFitting(o, 0x23, res, 1.0f);
}

static inline void putRoot(GObj *gobj, float *pos, float *lo, float *hi,
                           int clamp) /* derived name */
{
    float lim;
    float low;
    float d;

    lim = 50.0f;
    if (stage_no == 8) {
        lim = 80.0f;
    }
    pos[0] = test_CURRENTROOT(gobj)[0];
    pos[1] = test_CURRENTROOT(gobj)[1];
    pos[2] = test_CURRENTROOT(gobj)[2];
    low = lo[1] + lim;
    /* float 81 of the sub record, EE 0x144: root.step[1] */
    d = GOBJ_SUB(gobj)->root.step[1];
    pos[1] = pos[1] + d;
    if (clamp) {
        pos[1] = pos[1] < low ? low : (hi[1] < pos[1] ? hi[1] : pos[1]);
    }
    SetDirectRootPositionNoFitting(boyGObj, pos);
}

static inline void chainUpdate(GObj *gobj, float *sk, float *out, float *lo,
                               float *hi) /* derived name */
{
    GetSkeltonPosition(sk, gobj, 22);
    _InterGV(out, lo, hi, sk[1] - lo[1], hi[1] - sk[1]);
    SetChainRootUpdateMode(gobj, 2, out);
}

static void TestCageUpDown(GObj *cage, GObj *gobj)
{
    float vA[4];
    float vB[4];
    float vC[4];
    float vD[4];
    float vE[4];
    float vF[4];
    float vG[4];
    float vH[4];
    int mot = GOBJ_SUB(gobj)->ctrl.motion;

    GetCageChainPoint(vB, vC, cage);
    GOBJ_SUB(boyGObj)->root.ropeState = 0;
    switch (mot) {
    case 0x78:
        putRoot(gobj, vE, vB, vC, 1);
        chainUpdate(gobj, vG, vH, vB, vC);
        break;
    case 0x77:
        if (GOBJ_ACT(gobj)->actMode == 63) {
            putRoot(gobj, vE, vB, vC, 0);
            chainUpdate(gobj, vE, vF, vB, vC);
        } else {
            putRoot(gobj, vE, vB, vC, 1);
            chainUpdate(gobj, vE, vF, vB, vC);
        }
        break;
    case 0x79:
    case 0x7A:
        if (cageUpDown.last != mot) {
            initCage(gobj);
        }
        _InterGV(vA, cageUpDown.a, cageUpDown.b, (float)cageUpDown.cnt,
                 (float)(cageUpDown.lim - cageUpDown.cnt));
        vA[1] = vA[1] < vB[1] ? vB[1] : (vC[1] < vA[1] ? vC[1] : vA[1]);
        cageMove(cage, gobj, vF, vG, vH, vE, vA[0], vA[1], vA[2]);
        cageUpDown.cnt = cageUpDown.cnt + 1;
        chainUpdate(gobj, vE, vF, vB, vC);
        break;
    default:
        if (cageUpDown.last != mot) {
            GetSkeltonPosition((float *)&GOBJ_WORK(gobj)->handPosX, gobj, 22);
        }
        _InterGV(vD, vB, vC, GOBJ_WORK(gobj)->handPosY - vB[1], vC[1] - GOBJ_WORK(gobj)->handPosY);
        SetChainRootUpdateMode(gobj, 3, vD);
        break;
    }
    cageUpDown.last = mot;
}

typedef union { /* field names derived */
    float f[4];
    long long ll[2];
} RsVec4; /* derived name */

static inline unsigned char ropeSpecialWallHit(RsVec4 *p1, ClimbCol *hit) /* derived name */
{
    sceVu0FVECTOR va = {0.0f, 0.0f, -20.0f, 1.0f};
    sceVu0FVECTOR vb = {0.0f, 0.0f, 20.0f, 1.0f};
    ClipWork work;
    int i;

    /* the original sets only the two points; ClipWall reads the radius
       from the stack word: zero here */
    memset(&work, 0, sizeof(work));
    for (i = 0; i < 4; i++) {
        sceVu0UnitMatrix((void *)MatrixDrive_GetMatrix());
        MatrixDrive_TransMatrix(p1->f[0], p1->f[1] + 0.0f, p1->f[2]);
        MatrixDrive_RotMatrixY((short)(int)((float)i * 0.7853982f * 32768.0f / 3.1415927f));
        sceVu0ApplyMatrix(work.pt[0], (void *)MatrixDrive_GetMatrix(), va);
        sceVu0ApplyMatrix(work.pt[1], (void *)MatrixDrive_GetMatrix(), vb);
        ClipWall(&work);
        if (work.wall.elem != 0) {
            hit->wallSrc = work.wall.o;
            hit->wall = work.wall.elem;
            return 1;
        }
    }
    return 0;
}

void actCommonRopeSpecial(GObj *volatile self)
{
    Act *s;
    ClimbCol hit;
    RsVec4 p1;
    RsVec4 p2;
    RsVec4 pos;
    GObj *cage;
    unsigned char found;

    s = GOBJ_ACT(self);
    cage = s->env.cageObj.obj;
    if (cage != 0) {
        GOBJ_WORK(self)->ropeCage = cage;
    } else {
        cage = GOBJ_WORK(self)->ropeCage;
    }
    if (((int *)GOBJ_WORK(self)->modeHist)[0] == 4 || ((int *)GOBJ_WORK(self)->modeHist)[0] == 5) {
        GetSkeltonPosition((float *)&GOBJ_WORK(self)->handPosX, self, 35);
    } else {
        GetSkeltonPosition((float *)&GOBJ_WORK(self)->handPosX, self, 22);
    }
    GetCageChainPoint(p1.f, p2.f, cage);
    found = ropeSpecialWallHit(&p1, &hit);
    GOBJ_SUB(self)->root.ropeState = 0;
    while (1) {
        if (GOBJ_SUB(self)->ctrl.motion == 118) {
            s->flags20.ll &= ~(1ULL << 11);
        }
        GetSkeltonPosition(pos.f, self, 35);
        GetCageChainPoint(p1.f, p2.f, cage);
        if (debug_font_flag & 1) {
            debug_Printf(10, 120, 0xFFFFFFF, "%d, %d\n",
                         (int)*(float *)((char *)test_CURRENTROOT((void *)self) + 4), (int)p2.f[1]);
        }
        if (*(float *)((char *)test_CURRENTROOT((void *)self) + 4) > p2.f[1] - 30.0f) {
            ACTSendMailCorrect(self, 0xC7);
        }
        if (found && pos.f[1] < p1.f[1] + 60.0f) {
            GOBJ_ACT(self)->enemy->climbObj = cage;
            GetOrientOfWall((char *)GOBJ_ACT(self)->enemy->climbOrient, hit.wall, &hit.wallSrc);
            GOBJ_ACT(self)->enemy->climbPos[0] = p1.f[0];
            GOBJ_ACT(self)->enemy->climbPos[1] = p1.f[1];
            GOBJ_ACT(self)->enemy->climbPos[2] = p1.f[2];
            GOBJ_ACT(self)->enemy->climbPos[1] -= 100.0f;
            GOBJ_ACT(self)->enemy->climbCol = hit;
            ActSendMail_WithAdditionalData(self, 0xB0, (void *)self,
                                           GOBJ_ACT(self)->enemy->climbOrient);
        }
        TestCageUpDown(cage, (void *)self);
        _ACTWait(1);
    }
}

static void lever_nego1(GObj *self, GObj *lev)
{
    int m = lev->kind;
    if (m < 22) {
        return;
    }
    if (m < 24) {
        goto lever;
    }
    if (m >= 26) {
        return;
    }
    SetWallLeverWithNodePoint(lev, self, 0x16);
    return;
lever:
    SetFloorLeverWithNodePoint(lev, self, 0x16);
}

static void SetDirectRootPositionXZ(void *self, void *pos)
{
    void *ret = test_CURRENTROOT(self);
    *(float *)((char *)pos + 4) = *(float *)((char *)ret + 4);
    SetDirectRootPositionNoFitting(self, pos);
}

/* the turn away from the wall, which ACTMotDirToWall and the lever, box,
   bar and truck functions inline */
static inline void actMotDirToWall(GObj *self) /* derived name */
{
    float local[4];
    sceVu0ScaleVector(local,
                      ICO_RAWP(char *, ICO_RAW(char *, self, 0x164, ((struct GObj *)self)->act),
                               0x4B0, (char *)&GOBJ_ACT(self)->env),
                      -1.0f);
    SetMotionDirection(self, local);
}

static inline void correctLeverHoldPoint(GObj *self, GObj *lev) /* derived name */
{
    float w[4];
    if (lev->kind >= 22) {
        if (lev->kind < 24) {
            GetFloorLeverGlobalHoldPoint(w, lev);
        } else if (lev->kind < 26) {
            GetWallLeverGlobalHoldPoint(w, lev);
            debug_StdPrintfDummy("%f, %f, %f\n", w[0], w[1], w[2]);
        }
    }
    SetDirectRootPositionNoFittingWithNodePointXZ(self, 0x16, w, 0.2f);
}

void actCommonLever(GObj *volatile self)
{
    float p[4];
    Act *s = GOBJ_ACT(self);
    GObj *lev = s->env.pullObj;

    GOBJ_WORK(self)->leverTimer = ((60 - systemStatus[0] * 10) / systemStatus[1]) * 5;
    p[0] = s->env.pullPos[0];
    p[1] = s->env.pullPos[1];
    p[2] = s->env.pullPos[2];
    p[1] = test_CURRENTROOT((void *)self)[1];
    SetDirectRootPositionNoFittingWithNodePointXZ((void *)self, 0x2C, s->env.pullPos, 1.0f);
    actMotDirToWall(self);
    while (1) {
        if (lev != 0) {
            if (GOBJ_SUB(self)->ctrl.frameFlag2 != 0) {
                correctLeverHoldPoint((void *)self, lev);
            }
            if (GOBJ_SUB(self)->ctrl.frameFlag1 != 0) {
                lever_nego1((void *)self, lev);
            }
        }
        _ACTWait(1);
    }
}

static void EBRAIN_SEND_MES(void *enemy, int mes)
{
    if (enemy && ((struct GObj *)enemy)->kind == 4)
        eBrainSendMes(enemy, mes);
}

inline void actCommonPlay(GObj *volatile self)
{
    debug_StdPrintfDummy("enter actCommonPlay\n");
    _ACTWait(0);
}

static void DamageFunc(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    debug_StdPrintfDummy("damage\n");
    if (self != girlGObj) {
        s->life -= (float)GOBJ_ACT(self)->damage;
    }
    if (self->kind == 4) {
        EnemyBattleWork *b;
        EBRAIN_SEND_MES(self, 5);
        b = GOBJ_ACT(self)->enemy;
        EnemyDeleteParticle(self, b->hitDir, b->hitNodes);
    }
}

static void DownFunc(GObj *self)
{
    DamageFunc(self);
    if (self->kind == 1) {
        EBRAIN_SEND_MES((void *)GOBJ_ACT(self)->attacker, 6);
    }
}

inline void actCommonDamage(GObj *volatile self)
{
    debug_StdPrintfDummy("enter actCommonDamage\n");
    SetMotionDirection(self, GOBJ_ACT(self)->attackDir);
    DamageFunc(self);
    if (GOBJ_ACT(self)->enemy->liftKind == 3) {
        _ACTWait(360);
    }
    for (;;) {
        ACTSendMailCorrect(self, 0xC7);
        _ACTWait(1);
    }
}

void actCommonDown(GObj *volatile self)
{
    float v[4];
    Act *s = GOBJ_ACT(self);
    int notDamage = s->intrKind != 55 && s->intrKind != 56;

    debug_StdPrintfDummy("enter actCommonDown\n");
    ACT_AFTER_PROC(s) = (void (*)(GObj *))actAfterDown;
    if (notDamage) {
        if (self == boyGObj) {
            brainAddLevelGirl(1000.0f);
        }
        if (self == girlGObj) {
            sceVu0ScaleVector(v, GOBJ_ACT(self)->attackDir, -1.0f);
            SetMotionDirection(self, v);
        } else {
            SetMotionDirection(self, GOBJ_ACT(self)->attackDir);
        }
        DownFunc(self);
    }
    GOBJ_ACT(self)->enemy->liftLevel = 16;
    while (1) {
        if (self->kind != 4) {
            GOBJ_ACT(self)->hit = 1;
        }
        ACTSendMailCorrect(self, 0xC7);
        if (GOBJ_ACT(self)->enemy->liftLevel <= 0) {
            ACTSendMailCorrect(self, 0x124);
        }
        _ACTWait(1);
    }
}

static inline void dieNotifyObjects(GObj *self) /* derived name */
{
    void *g;

    if (self->labelId == 3757) {
        gamesysObjInfoCls(4, 0xEAD);
        gamesysObjInfoCls(0x21, 0xEAE);
    }
    g = isysGObjSearchFromObjKindID_begin(0x2F);
    if (g == 0) {
        g = isysGObjSearchFromObjKindID_begin(0x41);
    }
    if (self->kind == 4 && g != 0) {
        iosOmSendMail(g, 0x12, self);
    }
}

void actCommonDie(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    int cnt = 0;
    float t;
    int corpse;

    if (s->intrKind == 44) {
        corpse = 1;
    } else {
        corpse = 0;
    }
    debug_StdPrintfDummy("enter actCommonDie\n");
    SetMotionDirection(self, GOBJ_ACT(self)->attackDir);
    DownFunc(self);
    GOBJ_ACT(self)->hit = 1;
    dieNotifyObjects(self);
    if (self->labelId == 3757) {
        ResetReviveCountEnemy(self);
    }
    while (1) {
        if (corpse) {
            ACTSetPositionWithFitting((void *)self, test_CURRENTROOT((void *)self));
        }
        if (self->kind == 4) {
            _ACTWait(1);
            enemySetParticleDie(test_CURRENTROOT((void *)self), GOBJ_ACT(self)->attackDir);
            EnemySetfDisappearAll(self);
            actEnemyFlagOnDead(self);
            ico_gs_signal(ICO_GS_EV_ENEMY_KILLED, self->labelId);
            ACTGame_DeleteActorInformation(self);
            for (t = 0.0f; t < (float)(((60 - systemStatus[0] * 10) / systemStatus[1]) * 6);
                 t += GOBJ_WORK(self)->disappearSpeed) {
                float d = (t + t - (float)(((60 - systemStatus[0] * 10) / systemStatus[1]) * 6)) /
                          (float)(((60 - systemStatus[0] * 10) / systemStatus[1]) * 6);
                if (d < 0.01f) {
                    d = 0.01f;
                }
                SetEnemyDissolve(self, d);
                if ((int)((long long)s->flags20.ll >> 22) & 1) {
                    t += 10.0f;
                }
                _ACTWait(1);
            }
            actEnemyHyde(self);
            _ACTWait(0);
        }
        if (self == boyGObj || self == girlGObj) {
            if (((60 - systemStatus[0] * 10) / systemStatus[1]) * 2 < cnt) {
                ACT_LAYOUT_GAMEOVER();
                _ACTWait(0);
            }
        }
        cnt++;
        _ACTWait(1);
    }
}

static int Cling(GObj *self, int idx, ICO_WORD_PTR(GObj *) target)
{
    float q[4];
    ClingRec *r;
    int i;
    int v;

    r = &clingData[idx];
    memset(q, 0, 16);
    q[3] = 1.0f;
    if (!((unsigned int)idx < 15)) {
        debug_assert("src/commonact.c", 2733);
        __assert("src/commonact.c", 2733,
                 "index>=ClingDataID_cling_start && index<ClingDataID_cling_end");
    }
    for (i = 0; i < 3; i++) {
        v = (r->rot[i] << 15) / 180;
        if (v != 0) {
            switch (i) {
            case 0:
                RotQuaternionX(q, v);
                break;
            case 1:
                RotQuaternionY(q, v);
                break;
            case 2:
                RotQuaternionZ(q, v);
                break;
            }
        }
    }
    SetMotionNodeFixModeParameter((void *)self, (void *)target, r->mode, r->node, q, r->pos[0],
                                  r->pos[1], r->pos[2], 1.0f);
    return r->motion;
}

void actCommonCling(GObj *volatile self)
{
    int no;
    ICO_WORD_PTR(GObj *) tgt;

    no = (int)(_GetRandom() * 10.0f) % 15;
    tgt = GOBJ_ACT(self)->enemy->clingReq;

    GOBJ_ACT(self)->enemy->clingTarget = tgt;
    ACTGameCollisionOff((volatile int *)self);
    Cling(self, no, tgt);
    _ACTWait(0);
}

void actCommonSlip(GObj *volatile self)
{
    float dir[4];
    float v2[4];
    float v3[4];

    if (ICO_RAW(unsigned char, GOBJ_ACT(self)->enemy, 0x280, GOBJ_ACT(self)->enemy->slipFront) ==
        0) {
        dir[0] = GOBJ_ACT(self)->enemy->slipDirX;
        dir[1] = GOBJ_ACT(self)->enemy->slipDirY;
        dir[2] = GOBJ_ACT(self)->enemy->slipDirZ;
    } else {
        sceVu0ScaleVector(dir, (float *)&GOBJ_ACT(self)->enemy->slipDirX, -1.0f);
    }
    v2[0] = GOBJ_ACT(self)->enemy->slipDirX;
    v2[1] = GOBJ_ACT(self)->enemy->slipDirY;
    v2[2] = GOBJ_ACT(self)->enemy->slipDirZ;
    sceVu0ScaleVector(v3, (float *)&GOBJ_ACT(self)->enemy->slipDirX, 30.0f);
    v3[1] = -20.0f;
    while (1) {
        SetMotionDirectionSmooze(self, dir, 3.0f);
        _ACTWait(1);
    }
}

void actCommonStoneDead(GObj *volatile self)
{
    float center[4];
    float dir[4];
    Act *s = GOBJ_ACT(self);

    if (s->intrKind == 34) {
        SetBoyStonizedVisual(self);
        GetGameOverEffectCenterPosition(center);
        _OrientXZGV(dir, center, test_CURRENTROOT((void *)self));
        SetMotionDirection(self, dir);
    } else {
        ACTSetPositionWithFitting((void *)self, test_CURRENTROOT((void *)self));
        SetMotionDirection(self, GOBJ_ACT(self)->attackDir);
        self->drawMask = 0;
    }
    enable_game_pause = 0;
    scpBoyControlReadDisable = 1;
    _ACTWait(300);
    ACT_LAYOUT_GAMEOVER();
    _ACTWait(0);
}

typedef struct { /* field names derived */
    char pad0[20];
    int f14;
} ReviveSub; /* derived name */

inline void actCommonRevive(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter actCommonRevive\n");
    ACTGameCollisionOff((volatile int *)self);
    s->after = (void *)afterCommonRevive;
    SetDirectRootPositionNoFitting((void *)self, &s->restartPosX);
    EnemySetfAppearAll((void *)self);
    for (;;) {
        ResetEnemyPositionInfo((void *)self);
        ACTSendMailCorrect(self, 0xC7);
        _ACTWait(1);
    }
}

typedef struct { /* field names derived */
    char pad0[20];
    int f14;
    char pad18[52];
    int f4C;
} StoneSub; /* derived name */

void actCommonStone(GObj *volatile self)
{
    StoneSub *s = (StoneSub *)(char *)GOBJ_ACT(self);

    ((Act *)s)->after = (void *)afterCommonStone;
    ICO_RAW(int,
            ICO_RAW(char *, ICO_RAW(char *, self, 0x164, ((struct GObj *)self)->act), 0x680,
                    (char *)GOBJ_ACT(self)->enemy),
            0x2A0, GOBJ_ACT(self)->enemy->stonePair) = 0;
    while (1) {
        if (debug_font_flag & 1) {
            debug_Printf(10, 170, 0xFFFFFFF, "count =(%d)\n", GOBJ_ACT(self)->enemy->liftLevel);
        }
        if (debug_font_flag & 1) {
            debug_Printf(10, 180, 0xFFFFFFF, "level =(%d)\n", GOBJ_ACT(self)->enemy->stoneLevel);
        }
        switch (GOBJ_ACT(self)->enemy->stoneLevel) {
        case 0:
        case 1:
            break;
        case 2:
            _ACTParaStatus_Set(self, 0x26);
            break;
        default:
            _ACTParaStatus_Set(self, 0x27);
            break;
        }
        if (GOBJ_ACT(self)->enemy->liftLevel < 0) {
            ACTSendMailCorrect(self, 0xC7);
            GOBJ_ACT(self)->enemy->stoneLevel = 0;
        }
        if (ICO_RAW(int, s, 0x4C, GOBJ_ACT(self)->modeFrame) >= 0x3D &&
            GOBJ_ACT(self)->enemy->stoneLevel >= 3) {
            ACT_LAYOUT_GAMEOVER();
            _ACTWait(0);
        }
        ACTSendMailCorrect(self, 0x70);
        _ACTWait(1);
    }
}

void actCommonSofa(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);

    SetDirectRootPositionNoFitting((void *)self, s->env.sofaPos);
    GOBJ_ACT(self)->enemy->sofaWake = 0;
    GOBJ_ACT(self)->enemy->dirSmoothFrames = 0;
    GOBJ_ACT(self)->enemy->dirSmoothFrames2 = 0;
    s->sofa = *(void **)&s->env.sofaObj;
    while (1) {
        GOBJ_WORK(self)->sofaTimer = ((60 - systemStatus[0] * 10) / systemStatus[1]) * 3;
        GOBJ_WORK(self)->sofaRestTimer = ((60 - systemStatus[0] * 10) / systemStatus[1]) * 10;
        if (GOBJ_ACT(self)->enemy->sofaWake > GOBJ_ACT(self)->enemy->sofaWakeTime) {
            ACTSendMailCorrect(self, 0x73);
        }
        GOBJ_ACT(self)->enemy->sofaWake += 1;
        _ACTWait(1);
    }
}

static void BoxBarSoundOn(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    switch (s->actMode) {
    case 0x31:
        ExecBoxMoveStartReaction(s->box, (int)s->pushDir);
        break;
    case 0x33:
        ExecRotObjectMoveStartReaction(s->env.barObj);
        break;
    }
}

static void BoxBarSoundOff(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    switch (s->actMode) {
    case 0x31:
        ExecBoxMoveEndReaction(s->box);
        break;
    case 0x33:
        ExecRotObjectMoveEndReaction(s->env.barObj);
        break;
    }
}

static void _boxbar_set_sound(GObj *self, int mode)
{
    switch (GOBJ_ACT(self)->enemy->boxBarSound) {
    case 0:
        if (mode == 1) {
            BoxBarSoundOn(self);
            GOBJ_ACT(self)->enemy->boxBarSound = 1;
        }
        break;
    case 1:
        if (mode == 0 || mode == 2) {
            BoxBarSoundOff(self);
            GOBJ_ACT(self)->enemy->boxBarSound = mode;
        } else {
            BoxBarSoundOn(self);
        }
        break;
    case 2:
        if (mode == 0) {
            GOBJ_ACT(self)->enemy->boxBarSound = 0;
        }
        if (mode == 3) {
            BoxBarSoundOn(self);
            GOBJ_ACT(self)->enemy->boxBarSound = 1;
        }
        break;
    }
}

typedef void (*BoxAfterFn)(volatile int);

/* the height is an int */
static inline int boxWallCheck(GObj *self, GObj *box, float dist, int h) /* derived name */
{
    ClipWork w;
    float t[4];
    Act *s = GOBJ_ACT(self);

    GetRootPosition(w.pt[0], box);
    GetRootPosition(w.pt[1], box);
    sceVu0ScaleVector(t, s->env.wallOrient, dist);
    sceVu0AddVector(w.pt[1], w.pt[1], t);
    w.radius = h;
    w.pt[0][1] += 10.0f;
    w.pt[1][1] += 10.0f;
    ClipWall(&w);
    if (w.wall.elem == 0) {
        return 0;
    }
    return 1;
}

static inline void addGirlLevelForBox(GObj *b) /* derived name */
{
    if (girlGObj != 0 && GOBJ_SUB(girlGObj)->parent.obj == b) {
        brainAddLevelGirl(1000.0f);
    }
}

void actCommonBox(GObj *volatile self)
{
    GObj *box;
    Act *s = GOBJ_ACT(self);
    GObj *sub;
    float hold[4];

    s->after = afterCommonBox;
    box = s->env.holdBoxObj;
    if (stage_no == 16) {
        sub = GOBJ_SUB(box)->parent.obj;
        if (sub != 0) {
            if (sub->kind == 17) {
                box = sub;
            }
        }
    }
    ((Act *)(char *)s)->box = box;
    actMotDirToWall(self);
    sceVu0ScaleVector(GOBJ_WORK(self)->boxDir, s->env.wallOrient, -1.0f);
    while (1) {
        int had = (int)s->pushDir != 0;
        int f2 = 0;
        int miss = 0;
        int flag = 0;
        unsigned char isTruck = IsThisBoxTruck(box);

        addGirlLevelForBox(box);
        GetBoxHoldPoint(hold, box, self);
        if (!isTruck) {
            if (boxWallCheck(self, box, 100.0f, 48)) {
                ACTSendMailCorrect(self, 0xC7);
                miss = 1;
            }
            if (s->pushDir == 0xFFFFFFFF) {
                if (boxWallCheck(self, box, 150.0f, 48)) {
                    miss = 1;
                }
            }
        }
        if (!miss &&
            (isTruck == 0 ? GOBJ_SUB(self)->ctrl.trigger1 : GOBJ_SUB(self)->ctrl.frameFlag1)) {
            f2 = 1;
            if ((int)s->pushDir == 1 && boxWallCheck(self, box, -25.0f, 30)) {
                miss = 1;
            } else {
                float dir[4];
                unsigned char ok;
                if (s->pushDir == 0xFFFFFFFF) {
                    dir[0] = s->env.wallOrient[0];
                    dir[1] = s->env.wallOrient[1];
                    dir[2] = s->env.wallOrient[2];
                } else {
                    sceVu0ScaleVector(dir, s->env.wallOrient, -1.0f);
                }
                if (!isTruck) {
                    debug_StdPrintfDummy("A\n");
                    AlignBox(box, 100.0f);
                }
                ok = MoveBoxWithHoldPoint(box, hold, self, 22, dir);
                if (ok) {
                    flag = 1;
                } else {
                    miss = 1;
                }
            }
        }
        if (GetMotionFrameFlag2((void *)self)) {
            float pos[4];
            GetBoxGlobalHoldPoint(pos, box, hold);
            SetDirectRootPositionNoFittingWithNodePointXZ(
                (void *)self, 22, pos, GOBJ_SUB(self)->ctrl.frameFlag1 != 0 ? 1.0f : 0.3f);
        }
        if (!had) {
            _boxbar_set_sound(self, 0);
            if (!isTruck) {
                debug_StdPrintfDummy("B\n");
            }
        } else if (f2) {
            _boxbar_set_sound(self, 1);
        }
        if (flag) {
            _boxbar_set_sound(self, 3);
        }
        if (miss) {
            _boxbar_set_sound(self, 2);
        }
        if (!isTruck && GetBoxMode(box) == 0) {
            s->flags20.ll |= (1ULL << 40);
            s->flags20.ll |= (1ULL << 41);
        }
        _ACTWait(1);
    }
}

inline void afterCommonBar(GObj *volatile self)
{
    debug_StdPrintfDummy("reset\n");
    GOBJ_SUB(self)->root.filter = InitialColInfo;
    _boxbar_set_sound(self, 0);
}

typedef struct { /* field names derived */
    char pad0[448];
    int f1C0;
    int f1C4;
    int f1C8;
} BarHold; /* derived name */

void actCommonBar(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    GObj *bar;
    float pos[4];
    float ori[4];
    float hold[4];
    float hold2[4];

    bar = s->env.barObj;
    actMotDirToWall(self);
    pos[0] = *(float *)((char *)test_CURRENTROOT((void *)self) + 0);
    pos[1] = *(float *)((char *)test_CURRENTROOT((void *)self) + 4);
    pos[2] = *(float *)((char *)test_CURRENTROOT((void *)self) + 8);
    ori[0] = *(float *)((char *)test_CURRENTORIENT(self) + 0);
    ori[1] = *(float *)((char *)test_CURRENTORIENT(self) + 4);
    ori[2] = *(float *)((char *)test_CURRENTORIENT(self) + 8);
    GetRotObjectHoldPoint(hold, hold2, &GOBJ_WORK(self)->intrReq.a.wall, (void *)self);
    s->barObj = (ICO_WORD_PTR(GObj *))bar;
    s->after = (void *)afterCommonBar;
    debug_StdPrintfDummy("set %p\n", bar);
    GOBJ_SUB(self)->root.filter.o.obj = bar;
    GOBJ_SUB(self)->root.filter.o.node = -1;
    GOBJ_SUB(self)->root.filter.elem = 0;
    while (1) {
        int had = (int)s->pushDir != 0;
        int lit = 0;
        int miss = 0;
        int flag = 0;
        if (GetMotionFrameFlag1((void *)self)) {
            float dir[4];
            float up[4];
            int node = GetSkeltonFocusNode((void *)self, 32);
            unsigned char ok;
            lit = 1;
            CopyVector(dir, (char *)GOBJ_SUB(self)->nodeMtx + node * 64 + 0x30);
            if (s->pushDir == 0xFFFFFFFF) {
                sceVu0ScaleVector(up, test_CURRENTORIENT(self), -1.0f);
            } else {
                up[0] = *(float *)((char *)test_CURRENTORIENT(self) + 0);
                up[1] = *(float *)((char *)test_CURRENTORIENT(self) + 4);
                up[2] = *(float *)((char *)test_CURRENTORIENT(self) + 8);
            }
            ok = MoveRotObjectWithHoldPoint(bar, hold, (void *)self, dir, up);
            if (ok) {
                flag = 1;
            } else {
                miss = 1;
            }
        }
        if (GetMotionFrameFlag2((void *)self)) {
            GetRotObjectGlobalHoldGeometry(pos, ori, bar, hold, hold2);
            SetDirectRootPositionNoFittingWithNodePoint((void *)self, 0x20, pos, lit ? 1.0f : 0.3f);
            SetMotionDirection(self, ori);
        }
        if (!had) {
            _boxbar_set_sound(self, 0);
        } else if (lit && !miss) {
            _boxbar_set_sound(self, 1);
        }
        if (flag) {
            _boxbar_set_sound(self, 3);
        }
        if (miss) {
            _boxbar_set_sound(self, 2);
        }
        _ACTWait(1);
    }
}

void funcCommonJumpDircorrect(GObj *self)
{
    SetMotionDirection(self, GOBJ_WORK(self)->padWish);
}

void funcCommonFallDircorrect(GObj *self)
{
    SetMotionDirection(self, GOBJ_WORK(self)->fallDir);
}

static void correctJumpOrientByChain(GObj *self)
{
    float out[4];
    float mtx[16];
    float pos[4];
    float p[4];
    float q[4];
    float dir[4];
    GObj *o;
    float t;
    float best = 3.40282347e+38f; /* FLT_MAX */
    float ang = 0.0f;

    pos[0] = test_CURRENTROOT((void *)self)[0];
    pos[1] = test_CURRENTROOT((void *)self)[1];
    pos[2] = test_CURRENTROOT((void *)self)[2];
    GetMatrixDirectionToZ(mtx, test_CURRENTORIENT(self));

    for (o = isysGObjSearchFromObjKindID_begin(21); o != 0;
         o = isysGObjSearchFromObjKindID_next(o)) {
        if (o->active == 0) {
            continue;
        }
        GetRootPosition(p, o);
        sceVu0SubVector(q, p, pos);
        q[3] = 0.0f;
        sceVu0ApplyMatrix(q, mtx, q);
        if (q[2] < 0.0f || 1000.0f < q[2]) {
            continue;
        }
        if (GetChainHangRange(o) < (q[0] < 0.0f ? -q[0] : q[0])) {
            continue;
        }
        if (pos[1] < p[1]) {
            continue;
        }
        if (p[1] + GetChainLength(o) + 200.0f < pos[1]) {
            continue;
        }
        ang = q[0];
        best = q[2];
        if (ang < 0.0f)
            ang = -ang;
        out[0] = p[0];
        out[1] = p[1];
        out[2] = p[2];
    }
    if (best == 3.40282347e+38f) {
        return;
    }
    _OrientXZGV(dir, out, test_CURRENTROOT((void *)self));
    t = ang * 20.0f / 300.0f;
    if (t < 0.0f) {
        t = 0.0f;
    } else if (20.0f < t) {
        t = 20.0f;
    }
    SetMotionDirectionSmooze(self, dir, t);
}

typedef struct { /* field names derived */
    char pad0[398];
    unsigned short f18E;
} MotRecJ; /* derived name */

void actCommonJump(GObj *volatile self)
{
    float dir[4];
    Act *s = GOBJ_ACT(self);
    int chk = 0;
    int hit = 0;
    int n;

    s->flags18.ll &= ~(1ULL << 55);
    ACT_AFTER_PROC(s) = actAfterJump;
    if (s->intrKind == 82) {
        GOBJ_WORK(self)->jumpTimer = ((60 - systemStatus[0] * 10) / systemStatus[1]) * 10;
    }
    if (s->intrKind == 261) {
        s->flags18.ll |= (1ULL << 55);
        if (self == girlGObj && boyGObj != 0) {
            _OrientXZGV(dir, test_CURRENTROOT(boyGObj), test_CURRENTROOT((void *)self));
            SetMotionDirection(self, dir);
        }
    }
    s->flags18.ll &= ~(1ULL << 56);
    if (s->intrKind == 197) {
        SetMotionDirection(self, GOBJ_WORK(self)->boyOrient);
        s->flags18.ll |= (1ULL << 56);
    }
    n = ((int *)GOBJ_WORK(self)->modeHist)[0];
    if (n < 4) {
        if (0 < n) {
            switch ((unsigned int)s->intrKind) {
            case 0xBF:
            case 0xC0:
                chk = 1;
                break;
            case 0xBD:
                if (0.1f < s->stick.mag) {
                    chk = 1;
                }
                break;
            }
            if (chk != 0) {
                correctJumpOrientByChain(self);
            }
        }
    }
    while (1) {
        if (((MotRecJ *)&motionKind[GOBJ_SUB(self)->ctrl.motion])->f18E & 1) {
            hit = 1;
        }
        if (hit != 0 && motionKind[GOBJ_SUB(self)->ctrl.motion].playMode == 1) {
            GOBJ_SUB(self)->root.move[0] = GOBJ_SUB(self)->root.move[2] = 0.0f;
        }
        ACTSendMailCorrect(self, 0xBD);
        _ACTWait(1);
    }
}

/* The 0x15C slot is the engine's sub-object handle, read here as the SubHandle
 * union the way geometryManager.c's SUBOF reads it; the other readers use
 * GOBJ_SUB's int view. */
#define FALL_SUB(o) GOBJ_SUB(o)

/* A static inline that actCommonFall and flyCoreLoop call: whether the
 * actor has been stuck on a step for `need` hits.  flyCoreLoop passes a
 * motion that is not 418 and needs three hits where actCommonFall needs
 * two. */
static inline int IsFallStuckOnStep(GObj *self, int state, int mot, int need,
                                    int time) /* derived name */
{
    Act *s = GOBJ_ACT(self);
    int *q;
    int i, n, d;

    for (i = 0, n = 1, q = (int *)((ActWork *)s->work)->frameHist; i < 10; i++, q++) {
        if (q[-10] == state && (mot == 418 || mot == q[10])) {
            if (n >= need) {
                d = s->frame - *q;

                if (d < time * ((60 - systemStatus[0] * 10) / systemStatus[1]) / 60) {
                    return 1;
                }
                break;
            }
            n++;
        }
    }
    /* the not-found path returns the count variable, cleared */
    n = 0;
    return n;
}

void actCommonFall(GObj *volatile self)
{
    float pa[4];
    float pb[4];
    float hit[4];
    float lo[4];
    float hi[4];
    Act *s = GOBJ_ACT(self);
    int noVel;
    int wasHigh;
    int slowed;
    unsigned int mot;
    float d;
    char *p;
    int keep;
    int n;

    wasHigh = 0;
    noVel = 0;
    slowed = 0;
    ACT_AFTER_PROC((char *)s) = actAfterFall;
    if (stageData[stage_no].flag3) {
        keep = FALL_SUB(self)->ctrl.floorAttr & 0xF;
        debug_StdPrintfDummy("0x%8x -> 0x%8x\n", FALL_SUB(self)->ctrl.floorAttr, keep);
        FALL_SUB(self)->ctrl.floorAttr = 0;
        FALL_SUB(self)->ctrl.floorAttr = keep;
    } else {
        FALL_SUB(self)->ctrl.floorAttr = 0;
    }
    mot = (unsigned int)s->intrKind;
    switch (mot) {
    case 226:
        if (((int *)GOBJ_WORK(self)->modeHist)[0] == 34 ||
            ((int *)GOBJ_WORK(self)->modeHist)[0] == 28 ||
            ((int *)GOBJ_WORK(self)->modeHist)[0] == 30) {
            FALL_SUB(self)->root.move[0] = 0;
            noVel = 1;
            FALL_SUB(self)->root.move[1] = 0;
            FALL_SUB(self)->root.move[2] = 0;
        }
        break;
    case 329:
        SetMotionDirection(self, GOBJ_WORK(self)->handrailOrient);
        break;
    case 166:
        FALL_SUB(self)->root.move[0] = 0;
        FALL_SUB(self)->root.move[1] = 0;
        FALL_SUB(self)->root.move[2] = 0;
        break;
    case 24:
        FALL_SUB(self)->root.move[0] = 0;
        FALL_SUB(self)->root.move[1] = 0;
        FALL_SUB(self)->root.move[2] = 0;
        wasHigh = 1;
        noVel = 1;
        break;
    case 49:
        sceVu0ScaleVector(&FALL_SUB(self)->root.move[0], test_CURRENTORIENT(self), -5.0f);
        FALL_SUB(self)->root.move[1] = 3.0f;
        wasHigh = 1;
        ((ActStatus *)&s->flags18.ll)->ll |= (1ULL << 53);
        GOBJ_WORK(self)->fallDamageHeight = 200.0f;
        break;
    case 48:
        GetSkeltonPosition(pa, s->intrArg, 44);
        GetSkeltonPosition(pb, self, 44);
        lo[0] = pb[0];
        lo[1] = pb[1];
        lo[2] = pb[2];
        if (ACTCheckCollis_WAY(40.0f, pa, pb, 0, hit)) {
            lo[0] = pa[0];
            lo[1] = pa[1];
            lo[2] = pa[2];
            ACTSetPositionNoFitting(self, pa);
        }
        hi[0] = lo[0];
        hi[1] = lo[1];
        hi[2] = lo[2];
        lo[1] = lo[1] - 100.0f;
        hi[1] = hi[1] + 100.0f;
        if (ACTCheckCollis_WELL(lo, hi, 0, hit, 0.0f)) {
            hit[1] = hit[1] - 105.0f;
            ACTSetPositionNoFitting(self, hit);
        }
        break;
    case 316:
        d = GetDifferenceFromLowerField(self, 44);
        wasHigh = 1;
        ((ActStatus *)&s->flags18.ll)->ll |= (1ULL << 53);
        FALL_SUB(self)->root.move[0] = 0;
        FALL_SUB(self)->root.move[1] = 0;
        FALL_SUB(self)->root.move[2] = 0;
        if (d < 800.0f) {
            GOBJ_WORK(self)->fallDamageHeight = 900.0f;
        } else {
            if (((int *)GOBJ_WORK(self)->modeHist)[0] != 38) {
                p = (char *)GOBJ_ACT(self)->intrData;
                if (p != 0 && 100.0f < *(float *)p && *(float *)p < 500.0f) {
                    GOBJ_WORK(self)->fallDamageHeight = *(float *)p;
                    break;
                }
            }
            GOBJ_WORK(self)->fallDamageHeight = 500.0f;
        }
        break;
    default:
        if (mot == 7 && ((int *)GOBJ_WORK(self)->prevHist)[0] == 259) {
            FALL_SUB(self)->root.move[0] = 0;
            FALL_SUB(self)->root.move[1] = 0;
            FALL_SUB(self)->root.move[2] = 0;
            noVel = 1;
        }
        n = ((int *)GOBJ_WORK(self)->modeHist)[0];
        if (19 <= n) {
            if (22 <= n) {
                if (n < 29) {
                    if (27 <= n) {
                        FALL_SUB(self)->root.move[0] = 0;
                        FALL_SUB(self)->root.move[1] = 0;
                        FALL_SUB(self)->root.move[2] = 0;
                        noVel = 1;
                    }
                }
            } else {
                wasHigh = 1;
            }
        }
        if (self == girlGObj && mot == 7 && ((int *)GOBJ_WORK(self)->modeHist)[0] == 21) {
            slowed = 1;
            FALL_SUB(self)->root.move[1] = 0;
        }
        if ((unsigned char)IsFallStuckOnStep(self, 5, 418, 2, 60)) {
            ((Act *)(char *)s)->flags20.ll |= (1ULL << 42);
        }
        break;
    }
    while (1) {
        if (s->modeFrame < ((60 - systemStatus[0] * 10) / systemStatus[1]) / 6) {
            if (wasHigh) {
                ((ActStatus *)&s->flags18.ll)->ll |= (1ULL << 52);
            }
            if (noVel) {
                FALL_SUB(self)->root.move[0] = FALL_SUB(self)->root.move[2] = 0.0f;
            }
            if (slowed) {
                FALL_SUB(self)->root.move[0] =
                    ico_d2f(ico_dmul(ico_f2d(FALL_SUB(self)->root.move[0]), ICO_D(0.8)));
                FALL_SUB(self)->root.move[2] =
                    ico_d2f(ico_dmul(ico_f2d(FALL_SUB(self)->root.move[2]), ICO_D(0.8)));
            }
        }
        ACTSendMailCorrect(self, 314);
        _ACTWait(1);
    }
}

/* the sub-object's fly-limit flag at 0x654 */
typedef struct { /* field names derived */
    char pad0[1620];
    int limit;
} FlyLimitSub; /* derived name */

/* raises the flag that ResetFlyLimit clears */
static inline void SetFlyLimit(GObj *self) /* derived name */
{
    GOBJ_SUB(self)->ctrl.noFieldClip = 1; /* FlyLimitSub's 0x654 */
}

/* used by flyCoreLoop and actAfterFly */
static inline void ResetFlyLimit(GObj *self) /* derived name */
{
    GOBJ_SUB(self)->ctrl.noFieldClip = 0; /* FlyLimitSub's 0x654 */
}

/* clamp to -1..1 */
static inline float clampUnit(float x) /* derived name */
{
    if (1.0f < x) {
        x = 1.0f;
    }
    if (x < -1.0f) {
        x = -1.0f;
    }
    return x;
}

/* the vertical pull toward the target height */
static inline float calcVertAccel(float *a, float *b) /* derived name */
{
    return clampUnit((a[1] - b[1]) * 0.005f);
}

/* the vertical pull with a term for the horizontal distance */
static inline float calcFlyAccel(float *a, float *b) /* derived name */
{
    float d[4];
    float h;

    _SubVector(d, a, b);
    h = d[1];
    d[1] = 0.0f;
    h += -VectorLength(d) * (stage_no == 86 || stage_no == 3 || stage_no == 46 ? 2.5f : 0.5f);
    return clampUnit(h * 0.005f);
}

/* the flight-limit marker colour (R, G, B, A), the first of the four colour
   records at the head of commonact's .data colour run */
static int flyLimitCol[4] = {128, 192, 255, 128}; /* derived name */

typedef union { /* field names derived */
    float f[4];
    long long ll[2];
} FlyPt; /* derived name */

void debugDispFlyLimit(float *pos, float y0, float y1)
{
    MatrixDrive_PushMatrix();
    {
        FlyPt a = {{pos[0], y0, pos[2], 1.0f}};
        FlyPt b = {{pos[0], y1, pos[2], 1.0f}};

        _UnitMatrix(MatrixDrive_GetMatrix());
        gif_StartPacketPri(11);
        DrawLineG(&b, flyLimitCol, &a, flyLimitCol, 0);
        MatrixDrive_PushMatrix();
        MatrixDrive_TransMatrixV(&a);
        prim_DispWireSphere(10.0f, flyLimitCol, 4, 4);
        MatrixDrive_PopMatrix();
        MatrixDrive_PushMatrix();
        MatrixDrive_TransMatrixV(&b);
        prim_DispWireSphere(10.0f, flyLimitCol, 4, 4);
        MatrixDrive_PopMatrix();
        gif_EndPacket();
    }
    MatrixDrive_PopMatrix();
}

static void debugDispSphere(void *pos, void *col, float r)
{
    MatrixDrive_PushMatrix();
    _UnitMatrix(MatrixDrive_GetMatrix());
    gif_StartPacketPri(0xB);
    MatrixDrive_TransMatrixV(pos);
    prim_DispWireSphere(r, col, 4, 4);
    gif_EndPacket();
    MatrixDrive_PopMatrix();
}

/* the fly has run for more than 180 seconds */
static inline unsigned char IsFlyTimeOver(GObj *self) /* derived name */
{
    if ((60 - systemStatus[0] * 10) / systemStatus[1] * 180 < GOBJ_WORK(self)->carryGirlFrames) {
        return 1;
    }
    return 0;
}

static int getLandOffset(float *out, float *pos, short ang, float h)
{
    ClipWork w;

    memset(&w, 0, ICO_MAX_SIZE(ClipWork, 0xC0));
    _UnitMatrix(MatrixDrive_GetMatrix());
    MatrixDrive_TransMatrixV((char *)pos);
    MatrixDrive_RotMatrixY(ang);
    MatrixDrive_TransMatrix(0.0f, 0.0f, h);
    CopyVector(w.pt[1], (char *)MatrixDrive_GetMatrix() + 0x30);
    CopyVector(w.pt[0], pos);
    w.pt[0][3] = w.pt[1][3] = 1.0f;
    w.pt[0][1] -= 50.0f;
    w.pt[1][1] -= 50.0f;
    ClipWall(&w);
    if (w.wall.elem) {
        return 0;
    }
    CopyVector(w.pt[0], w.pt[1]);
    w.pt[1][1] += 100.0f;
    ClipFloor(&w);
    if (w.floor.elem) {
        _SubVectorXYZ(out, w.pt[2], pos);
        return 1;
    }
    return 0;
}

static inline void RequestFlyClip(ClipColReq *req, void (*func)(ClipWork *), float mat[4][4],
                                  float root[4]) /* derived name */
{
    req->func = func;
    req->clip.radius = 50.0f;
    CopyVector(req->clip.pt[0], mat[3]);
    CopyVector(req->clip.pt[1], root);
    req->obj = 0;
    RequestClipCollision(req);
}

static inline void FlyStep(GObj *self, Act *act, int checkStuck, float lenSq, float root[4],
                           float mat[4][4], float dir[4], float vC0[4], int *needInit, float *acc,
                           int *mode, float *fc) /* derived name */
{
    if (*needInit) {
        RequestFlyClip((ClipColReq *)act->flyClip,
                       checkStuck ? ClipCollisionWithField : ClipCollision, mat, root);
        *needInit = 0;
    } else if (((ClipColReq *)act->flyClip)->done) {
        if (((ClipColReq *)act->flyClip)->clip.wall.elem ||
            ((ClipColReq *)act->flyClip)->clip.floor.elem) {
            if (10000.0f < VectorLengthSquare(dir)) {
                *acc = -1.0f;
            } else if (0.0f < vC0[1]) {
                *acc = 1.0f;
            } else {
                *acc = -1.0f;
            }
        } else {
            *acc = calcFlyAccel(root, mat[3]);
        }
        RequestFlyClip((ClipColReq *)act->flyClip,
                       checkStuck ? ClipCollisionWithField : ClipCollision, mat, root);
    }
    {
        FlyLimitInfo info;

        if (GetFlyLimitHeight(&info, mat[3])) {
            if (debug_fly_limit_test) {
                int save = debug_font_flag;

                debugDispFlyLimit(mat[3], info.limitY, info.floorY);
                debug_font_flag = 1;
                debug_Printf(10, 160, 0xFFFFFF00, "[%s] %4d %4d %4d", "limit", (int)info.floorY,
                             (int)info.limitY, (int)info.limitOfs);
                debug_font_flag = save;
            }
            if (lenSq < 90000.0f && (info.floorY < root[1] || root[1] < info.limitY)) {
                ClipWork w = {{{0.0f}}, {{0.0f}}, 50.0f};

                CopyVector(w.pt[0], mat[3]);
                w.pt[0][1] += 100.0f;
                _ApplyMatrix(w.pt[1], mat, ZUnitVector);
                w.pt[1][1] = 0.0f;
                _NormalizeVector(w.pt[1], w.pt[1]);
                _ScaleVectorXYZ(w.pt[1], w.pt[1], 200.0f);
                _AddVectorXYZ(w.pt[1], w.pt[0], w.pt[1]);
                ClipWall(&w);
                if (w.wall.elem) {
                    MatrixDrive_PushMatrix();
                    CopyMatrix(MatrixDrive_GetMatrix(), mat);
                    if (stage_no == 19 || stage_no == 28) {
                        MatrixDrive_RotMatrixY(-24576);
                    } else {
                        MatrixDrive_RotMatrixY(4096);
                    }
                    _ApplyMatrix(dir, MatrixDrive_GetMatrix(), ZUnitVector);
                    dir[1] = 0.0f;
                    _NormalizeVector(dir, dir);
                    MatrixDrive_PopMatrix();
                    SetMotionDirection(self, dir);
                    *acc = -1.0f;
                } else if (info.floorY < root[1]) {
                    *acc = 1.0f;
                    *mode = 1;
                    *fc = info.floorY;
                } else {
                    *acc = -1.0f;
                    *mode = 2;
                    *fc = info.limitY;
                }
                if (debug_fly_limit_test) {
                    static int col[4] = {0, 255, 128, 128};
                    debugDispSphere(mat[3], col, 100.0f);
                }
            } else {
                _ACTMotDirSmzDirect(self, dir);
            }
            if (info.limitY > mat[3][1]) {
                *acc = clampUnit((info.limitY - mat[3][1]) * 0.05f);
            }
        } else if (*mode == 0) {
            _NormalizeVector(dir, dir);
            _ACTMotDirSmzDirect(self, dir);
        } else {
            *acc = clampUnit((root[1] - mat[3][1]) * 0.005f);
            if (360000.0f < lenSq) {
                *mode = 0;
            }
            if (*mode == 1) {
                *acc = 1.0f;
                if (*fc < mat[3][1]) {
                    *mode = 0;
                }
            } else {
                *acc = -1.0f;
                if (mat[3][1] < *fc) {
                    *mode = 0;
                }
            }
            if (debug_fly_limit_test) {
                static int col[4] = {0, 0, 128, 128};
                debugDispSphere(mat[3], col, 100.0f);
            }
        }
    }
}

static int completeEmergency(GObj *self, float spd, int *cnt104)
{
    if (debug_fly_limit_test) {
        debug_StdPrintfDummy("EMERGENCY COMPLETE CHECK : SPEEDSQ:%f LENSQ:%f\n",
                             VectorLengthSquare((char *)GOBJ_SUB(self)->root.move), spd);
    }
    if (stage_no != 86 && stage_no != 3 && stage_no != 46) {
        if (VectorLengthSquare((char *)GOBJ_SUB(self)->root.move) < 50.0f && spd < 1000.0f) {
            return 1;
        }
    } else {
        if (VectorLengthSquare((char *)GOBJ_SUB(self)->root.move) < 300.0f && spd < 7000.0f) {
            *cnt104 = 0;
            return 1;
        }
    }
    return 0;
}

static int emergencyCheck(GObj *self, int *wait, float ring[5][4], int *ringidx, int *ringcnt,
                          float mat[4][4], int cnt114, int *flags)
{
    float prev[4];
    float mx;
    float d;
    int i;

    if (*wait == 0) {
        mx = 0.0f;
        CopyVector(prev, ring[*ringidx]);
        CopyVector(ring[*ringidx], mat[3]);
        if (*ringcnt < 5) {
            for (i = 0; i < *ringcnt; i++) {
                d = distance_squared(ring[0], ring[i]);
                if (mx < d) {
                    mx = d;
                }
            }
        } else {
            for (i = 0; i < 5; i++) {
                d = distance_squared(prev, ring[i]);
                if (debug_fly_limit_test) {
                    debug_StdPrintfDummy("%1.1f ", d);
                }
                if (mx < d) {
                    mx = d;
                }
            }
        }
        *ringcnt = *ringcnt + 1;
        *ringidx = *ringidx + 1;
        if (*ringidx == 5) {
            *ringidx = 0;
        }
        if (debug_fly_limit_test) {
            debug_StdPrintfDummy("EMERGENCY CHECK %d(%d): MAX: %f\n", cnt114, *ringcnt, mx);
        }
        if (*ringcnt >= 5 && mx < 10000.0f) {
            debug_StdPrintfDummy("\x1b[36mEMERGENCY WITH NO MOVE\x1b[m\n");
            *flags |= 2;
            return 1;
        }
        if (debug_fly_limit_test == 0 && IsFlyTimeOver(self)) {
            debug_StdPrintfDummy("\x1b[36mEMERGENCY WITH TIME OUT\x1b[m\n");
            *flags |= 4;
            return 1;
        }
    }
    *wait = *wait + 1;
    if (*wait == 30) {
        *wait = 0;
    }
    return 0;
}

static void flyCoreLoop(GObj *self, GObj *target, int checkStuck)
{
    Act *act = GOBJ_ACT(self);
    float lenSq;
    float spd;
    unsigned long long stuck =
        checkStuck && IsFallStuckOnStep(self, 6, debug_fly_limit_test ? 29 : 30, 3, 600);
    int flags = 0;
    int ringidx;
    int landCnt = 0;
    short ang = 0;
    int landed = 0;
    float off[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    float acc = 0.0f;
    float emgpos[4];
    int needInit = 1;
    int mode = 0;
    float fc = 0.0f;
    float ring[5][4];
    float mat[4][4];
    int wait = 0;
    int dbg = 0; /* local debug switch, see the test after spd below */
    int cnt104 = 0;
    float root[4];
    int cnt114 = 0;
    float vC0[4];
    float dir[4];
    int ringcnt = 0;
    for (ringidx = 0; ringidx < 5; ringidx++) {
        CopyVector(ring[ringidx], ZeroPoint);
    }
    ringidx = 0;
    if (stuck) {
        debug_StdPrintfDummy("\x1b[36mEMERGENCY WITH DANGER LOOP\x1b[m\n");
        flags |= 1;
        SetFlyLimit(self);
    }
    if (stage_no == 86 || stage_no == 3 || stage_no == 46) {
        emgpos[0] = GOBJ_WORK(self)->emgPosX;
        emgpos[1] = GOBJ_WORK(self)->emgPosY;
        emgpos[2] = GOBJ_WORK(self)->emgPosZ;
    }
    while (1) {
        GOBJ_SUB(self)->root.fieldWall = checkStuck;
        GetRootMotionMatrix(mat, self);
        if (target) {
            GetRootPosition(root, target);
            if (stage_no == 86 || stage_no == 3 || stage_no == 46) {
                root[0] = emgpos[0];
                root[1] = emgpos[1];
                root[2] = emgpos[2];
            }
            if (stuck) {
                root[1] += -100.0f;
            } else {
                root[1] += GOBJ_SUB(target)->root.projHeight;
                if ((60 - systemStatus[0] * 10) / systemStatus[1] < landCnt++ || !landed) {
                    landed = getLandOffset(off, root, ang, 100.0f);
                    if (!landed) {
                        ang += 4096;
                    }
                    landCnt = 0;
                }
                if (landed) {
                    _AddVectorXYZ(root, root, off);
                }
                root[1] -= 10.0f;
            }
            if (debug_fly_limit_test) {
                static int col[4] = {0, 128, 255, 128};
                debugDispSphere(root, col, 10.0f);
            }
        } else {
            CopyVector(root, ZeroPoint);
        }
        _SubVector(vC0, root, mat[3]);
        CopyVector(dir, vC0);
        dir[1] = 0.0f;
        lenSq = VectorLengthSquare(dir);
        spd = distance_squared(root, mat[3]);
        /* a local debug switch, off: print the distance */
        if (dbg) {
            debug_StdPrintfDummy("%1.1f ", lenSq);
        }
        if (stuck) {
            int y = 30;

            if (flags & 2)
                debug_PrintfDummy(10, y += 10, 0x80FF0080, "EMERGENCY BY NOMOVE");
            if (flags & 4)
                debug_PrintfDummy(10, y += 10, 0x80FF0080, "EMERGENCY BY TIMEOUT");
            if (flags & 1)
                debug_PrintfDummy(10, y += 10, 0x80FF0080, "EMERGENCY BY DANGER LOOP");
            acc = calcVertAccel(root, mat[3]);
            _ACTMotDirSmzDirect(self, dir);
            if (stage_no != 86 && stage_no != 3 && stage_no != 46) {
                SetDarkVolumeEffect(mat[3], 100.0f);
            }
            if (completeEmergency(self, spd, &cnt104)) {
                ResetFlyLimit(self);
                debug_StdPrintfDummy("%p complete.\n", self);
                stuck = 0;
            }
        } else {
            FlyStep(self, act, checkStuck, lenSq, root, mat, dir, vC0, &needInit, &acc, &mode, &fc);
            if (checkStuck && ((int)(act->flags20.ll >> 21) & 1) == 0 &&
                emergencyCheck(self, &wait, ring, &ringidx, &ringcnt, mat, cnt114, &flags)) {
                SetFlyLimit(self);
                stuck = 1;
            }
        }
        _ScaleVectorXYZ((char *)GOBJ_SUB(self)->root.move, (char *)GOBJ_SUB(self)->root.move,
                        0.92f);
        if (((int)(act->flags20.ll >> 21) & 1) == 0) {
            FlyPt v = {{0.0f, 0.0f,
                        stage_no == 86 || stage_no == 3 || stage_no == 46
                            ? 3.0f
                            : (checkStuck ? GetEnemyFlyXZAccel(self) : 1.0f),
                        0.0f}};

            _ApplyMatrix(&v, mat, &v);
            v.f[1] = 0.0f;
            _AddVectorXYZ((char *)GOBJ_SUB(self)->root.move, (char *)GOBJ_SUB(self)->root.move, &v);
            GOBJ_SUB(self)->root.move[1] += acc * 2.0f;
        }
        if ((stage_no == 86 || stage_no == 3 || stage_no == 46) &&
            (60 - systemStatus[0] * 10) / systemStatus[1] * 10 < cnt104) {
            SetFlyLimit(self);
            stuck = 1;
        }
        act->flags18.ll = (act->flags18.ll & ~(1ULL << 57)) | (stuck << 57);
        _ACTWait(1);
        cnt114++;
        cnt104++;
    }
}

typedef struct { /* field names derived */
    char pad0[1528];
    int f5F8;
} FlyCtlJ; /* derived name */

void actCommonFly(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    GObj *target;
    GObj *gen = 0;

    s->flags18.ll &= ~(1ULL << 57);
    ACT_AFTER_PROC(s) = actAfterFly;

    GOBJ_SUB(self)->ctrl.floorAttr = 0; /* FlyCtlJ's 0x5F8 */

    if (IsEnemyBrainToGenerator(self, &gen)) {
        target = gen;
    } else if (IsEnemyBrainToBoy(self) && boyGObj != 0) {
        target = boyGObj;
    } else if (girlGObj != 0) {
        target = girlGObj;
    } else if (boyGObj != 0) {
        target = boyGObj;
    } else {
        target = 0;
    }

    SetEnemyFootPrintSwitch(self, 0);

    flyCoreLoop(self, target,
                (girlGObj != 0 && GOBJ_ACT(girlGObj)->actMode == 111 &&
                 GOBJ_ACT(girlGObj)->carrier == self) ||
                    debug_fly_limit_test != 0);
}

typedef struct { /* field names derived */
    int upEnd;   /* 1 or 2 when the field above ends by node 22 or 6 (EnemyBattleWork 0x290) */
    int downEnd; /* 1 or 2 when the field below ends by node 52 or 48 (EnemyBattleWork 0x294) */

    union {
        unsigned long long ll;
        int i[2];
    } moveFlags; /* EnemyBattleWork 0x298: word298 and stoneLevel, one doubleword */
} LadderWork;    /* derived name */

/* the view starts at the enemy work's ladderUpStep, 0x290 on the EE */
#define LADW ((LadderWork *)&GOBJ_ACT(self)->enemy->ladderUpStep)

void actCommonLadder(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    int mot = s->intrMot;
    float pos[4];
    int uf22;
    int uf6;
    int lf52;
    int lf48;
    int up22;
    int up6;
    int lp52;
    int lp48;
    GObj *o;

    if (s->env.edgePos[3] != 0.0f) {
        pos[0] = test_CURRENTROOT((void *)self)[0];
        pos[1] = test_CURRENTROOT((void *)self)[1];
        pos[2] = test_CURRENTROOT((void *)self)[2];
        pos[0] = s->env.edgePos[0];
        pos[2] = s->env.edgePos[2];
        s->env.edgePos[3] = 0.0f;
    }
    LADW->upEnd = 0;
    LADW->downEnd = 0;
    LADW->moveFlags.ll &= ~1ULL;
    LADW->moveFlags.ll &= ~2ULL;
    LADW->moveFlags.ll &= ~4ULL;
    if (self == boyGObj) {
        GOBJ_ACT(self)->hit = 1;
    }
    if (mot == 127 || mot == 128 || mot == 123) {
        while (1) {
            if (self == boyGObj && GOBJ_SUB(self)->ctrl.animFrame > 40.0f) {
                break;
            }
            if (motionKind[GOBJ_SUB(self)->ctrl.motion].playMode == 1) {
                break;
            }
            _ACTWait(1);
        }
    }
    while (1) {
        uf22 = (int)GetDifferenceFromWallUpperField((void *)self, 22);
        uf6 = (int)GetDifferenceFromWallUpperField((void *)self, 6);
        lf52 = (int)GetDifferenceFromLastField((void *)self, 52);
        lf48 = (int)GetDifferenceFromLastField((void *)self, 48);
        up22 = (int)GetDifferenceFromWallUpperPlane((void *)self, 22);
        up6 = (int)GetDifferenceFromWallUpperPlane((void *)self, 6);
        lp52 = (int)GetDifferenceFromWallLowerPlane((void *)self, 52);
        lp48 = (int)GetDifferenceFromWallLowerPlane((void *)self, 48);
        LADW->upEnd = 0;
        LADW->downEnd = 0;
        LADW->moveFlags.ll &= ~4ULL;
        LADW->moveFlags.ll |= 1ULL;
        LADW->moveFlags.ll |= 2ULL;
        if ((uf22 - up22 < 0 ? up22 - uf22 : uf22 - up22) < 30) {
            if (uf22 < 10) {
                LADW->upEnd = 1;
            }
            if (uf6 < 10) {
                LADW->upEnd = 2;
            }
        } else {
            if (up22 < 40) {
                LADW->moveFlags.ll &= ~1ULL;
            }
            if (up6 < 40) {
                LADW->moveFlags.ll &= ~1ULL;
            }
        }
        if ((lf52 - lp52 < 0 ? lp52 - lf52 : lf52 - lp52) < 30) {
            if (lf52 >= -9) {
                LADW->downEnd = 1;
            }
            if (lf48 >= -9) {
                LADW->downEnd = 2;
            }
        } else {
            if (lp52 >= 31) {
                LADW->moveFlags.ll |= 4ULL;
            }
            if (lp48 >= 31) {
                LADW->moveFlags.ll |= 4ULL;
            }
        }
        if (s->pushDir == 0xFFFFFFFF && ((int)((long long)LADW->moveFlags.ll >> 2) & 1)) {
            ACTSendMailCorrect(self, 0x8E);
            if (self != boyGObj) {
                ACTSendMailCorrect(self, 0x8F);
            }
        }
        if (self == boyGObj && girlGObj != 0 && GOBJ_ACT(girlGObj)->actMode == 38) {
            if (_DistSqGV(test_CURRENTROOT((void *)self), test_CURRENTROOT(girlGObj)) < 3600.0f) {
                ACTSendMailCorrect(self, 0x31);
            }
        }
        if (self == boyGObj || self == girlGObj) {
            pos[0] = test_CURRENTROOT((void *)self)[0];
            pos[1] = test_CURRENTROOT((void *)self)[1];
            pos[2] = test_CURRENTROOT((void *)self)[2];
            for (o = isysGObjSearchFromObjKindID_begin(4); o != 0;
                 o = isysGObjSearchFromObjKindID_next(o)) {
                if (o->active != 0) {
                    if (GOBJ_ACT(o)->actMode == 38 &&
                        _DistSqGV(pos, test_CURRENTROOT(o)) < 6400.0f) {
                        ACTSendMailCorrect(self, 0x31);
                        break;
                    }
                }
            }
        }
        _ACTWait(1);
    }
}

/* inline tail members, defined here: their strings come between the
   flyCoreLoop unit's and funcCommonBeginReady's in .rodata, while their code
   goes to the end of the object */
inline void actCommonCliffdown(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter actCommonCliffdown\n");
    SetMotionDirection(self, s->env.cliffOrient);
    for (;;) {
        ACTSendMailCorrect(self, 0xC7);
        _ACTWait(1);
    }
}

inline void actCommonShoal(GObj *volatile self)
{
    debug_StdPrintfDummy("act main shoal\n");
    _ACTWait(0);
}

inline void actCommonSwim(GObj *volatile self)
{
    debug_StdPrintfDummy("enter actCommonSwim\n");
    for (;;) {
        _ACTWait(1);
    }
}

inline void actCommonDodge(GObj *volatile self)
{
    float dir[4];
    ICO_WORD id = 0;
    Act *s = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter actCommonDodge\n");
    if (self->kind == 1) {
        ACTSearchEnemy((void *)self, &id, dir);
    } else {
        _OrientXZGV(s->dir, test_CURRENTROOT(boyGObj), test_CURRENTROOT((void *)self));
        SetMotionDirection(self, s->dir);
    }
    while (1) {
        if (id != 0) {
            SetMotionDirectionWithLimit((void *)self, dir, 10.0f, 90.0f);
        }
        ACTSendMailCorrect(self, 0xC7);
        _ACTWait(1);
    }
}

inline void actCommonGuard(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);

    debug_StdPrintfDummy("enter actCommonGuard\n");
    SetMotionDirection(self, GOBJ_ACT(self)->attackDir);
    if (self->kind == 4) {
        EBRAIN_SEND_MES((void *)self, 6);
        ACTGame_LwsEffect_Guard((void *)self);
    }
    iosOmSendMail(boyGObj, 0x11C, self);
    if (s->intrKind == 282) {
        ACTSetPositionWithFitting((void *)self, test_CURRENTROOT((void *)self));
    }
    for (;;) {
        ACTSendMailCorrect(self, 0xC7);
        _ACTWait(1);
    }
}

#undef LADW

void actCommonEdgeHang(GObj *volatile self)
{
    ClipWork work;
    float p1[4];
    float p2[4];

    ACTAdjustPlane(self, &GOBJ_WORK(self)->intrReq.a.wall);
    while (1) {
        if (CheckWallAttributeEdegWall(self) == 0) {
            ACTSendMailCorrect(self, 0xE2);
        }
        if (-GOBJ_SUB(self)->ctrl.cliffDepth > 250.0f) {
            ACTSendMailCorrect(self, 0x18);
        }
        if (stageData[stage_no].flag2) {
            memset(&work, 0, ICO_MAX_SIZE(ClipWork, 0xC0));
            GetSkeltonPosition(work.pt[0], self, 0x2C);
            GetSkeltonPosition(p1, self, 0x33);
            GetSkeltonPosition(p2, self, 0x2F);
            sceVu0AddVector(work.pt[1], p1, p2);
            sceVu0ScaleVector(work.pt[1], work.pt[1], 0.5f);
            ClipFloor(&work);
            if (work.floor.elem != 0) {
                ACTSendMailCorrect(self, 0xE2);
            }
        }
        _ACTWait(1);
    }
}

inline void motCommonHangNone(GObj *volatile self)
{
    debug_StdPrintfDummy("enter motCommonHang None\n");
    _ACTWait(0);
}

inline void motCommonHangWall(GObj *volatile self)
{
    debug_StdPrintfDummy("enter motCommonHang Wall\n");
    _ACTWait(0);
}

inline void motCommonHangCliff(GObj *volatile self)
{
    debug_StdPrintfDummy("enter motCommonHang Cliff\n");
    _ACTWait(0);
}

inline void motCommonNull(GObj *volatile self)
{
    debug_StdPrintfDummy("enter motCommonNull\n");
    for (;;) {
        _ACTWait(1);
    }
}

void funcCommonBeginReady(char *self, int mail, char *from)
{
    GOBJ_ACT(self)->readyFlags |= 1;
    debug_StdPrintfDummy("ready begin %s to %s\n", from == (char *)boyGObj ? "boy" : "girl",
                         self == (char *)boyGObj ? "boy" : "girl");
}

void funcCommonEndReady(char *self, int mail, char *from)
{
    GOBJ_ACT(self)->readyFlags |= 2;
    debug_StdPrintfDummy("ready end %s to %s\n", from == (char *)boyGObj ? "boy" : "girl",
                         self == (char *)boyGObj ? "boy" : "girl");
}

void funcCommonEndExec(char *self, int mail, char *from)
{
    GOBJ_ACT(self)->readyFlags |= 8;
    debug_StdPrintfDummy("exec end %s to %s\n", from == (char *)boyGObj ? "boy" : "girl",
                         self == (char *)boyGObj ? "boy" : "girl");
}

void funcCommonError(char *self, int mail, char *from)
{
    GOBJ_ACT(self)->readyFlags |= 0x10;
    debug_StdPrintfDummy("????error %s to %s\n", from == (char *)boyGObj ? "boy" : "girl",
                         self == (char *)boyGObj ? "boy" : "girl");
}

int SetMotionDirectionSmooze(GObj *self, float *dir, float s)
{
    float v[4];
    Act *sub = GOBJ_ACT(self);
    int ret = 0;
    int r;

    if (s < 0.0f) {
        return 0;
    }
    s = s * 60.0f / (float)((60 - systemStatus[0] * 10) / systemStatus[1]);
    if (dir[0] == 0.0f && dir[1] == 0.0f && dir[2] == 0.0f) {}
    if (((motionKind + GOBJ_SUB(self)->ctrl.motion)->flags.word >> 6) & 1 &&
        ((long long *)((ActWork *)sub->work)->modeHist)[0] == 0x1A00000005LL) {
        s = 30.0f;
    }
    if ((int)(sub->flags20.ll >> 33) & 1) {
        s = ((ActWork *)sub->work)->lockedMaxRotate;
    }
    r = _RotyGV(test_CURRENTORIENT(self), dir);
    if ((float)(r < 0 ? -r : r) < s) {
        ret = 1;
        v[0] = dir[0];
        v[1] = dir[1];
        v[2] = dir[2];
    } else if (r > 0) {
        v[0] = test_CURRENTORIENT(self)[0];
        v[1] = test_CURRENTORIENT(self)[1];
        v[2] = test_CURRENTORIENT(self)[2];
        _ApplyRyGV(v, -s * 3.1415927f / 180.0f);
    } else {
        v[0] = test_CURRENTORIENT(self)[0];
        v[1] = test_CURRENTORIENT(self)[1];
        v[2] = test_CURRENTORIENT(self)[2];
        _ApplyRyGV(v, s * 3.1415927f / 180.0f);
    }
    SetMotionDirection(self, v);
    return ret;
}

/* motionOrientManager.h declares none of the motion tables */
extern MotOriName motionOriKind[];

void _ACTDebugPrint(GObj *self)
{
    Act *sub;
    char *w;

    if (self == 0) {
        return;
    }
    sub = GOBJ_ACT(self);
    if ((char *)sub == 0) {
        return;
    }
    w = (char *)&GOBJ_SUB(self)->ctrl.stream;
    if (w == 0) {
        return;
    }
    if (debug_font_flag & 1) {
        debug_Printf(30, 90, 0xFFFFFFF, " ori  = [%s]\n",
                     motionOriKind[((struct MotCtrl *)w)->orientKind].s);
        if (debug_font_flag & 1) {
            debug_Printf(30, 100, 0xFFFFFFF, " mot  = [%s]\n",
                         (motionKind + GOBJ_SUB(self)->ctrl.motion)->name);
            if (debug_font_flag & 1) {
                debug_Printf(30, 110, 0xFFFFFFF, " mode = [%s]\n", actModeTbl[sub->actMode].name);
                if (debug_font_flag & 1) {
                    debug_Printf(30, 120, 0xFFFFFFF, "frame = [%f]\n",
                                 GOBJ_SUB(self)->ctrl.animFrame);
                    if (debug_font_flag & 1) {
                        debug_Printf(30, 130, 0xFFFFFFF, "maxry = [%d]\n",
                                     self == girlGObj && girlControlMode != 0
                                         ? (motionKind + GOBJ_SUB(self)->ctrl.motion)->girlDirFrames
                                         : (motionKind + GOBJ_SUB(self)->ctrl.motion)->dirFrames);
                        if (debug_font_flag & 1) {
                            debug_Printf(30, 140, 0xFFFFFFF, " life = [%d]\n", (int)sub->life);
                            if (debug_font_flag & 1) {
                                debug_Printf(
                                    30, 150, 0xFFFFFFF, "   dw = [%d] [%d]\n",
                                    (int)((struct MotCtrl *)sub->motReq)->wallDist,
                                    (int)-((struct MotCtrl *)sub->motReq)->wallFloorHeight);
                                if (debug_font_flag & 1) {
                                    debug_Printf(30, 160, 0xFFFFFFF, "   dc = [%d] [%d]\n",
                                                 (int)((struct MotCtrl *)sub->motReq)->cliffDist,
                                                 (int)((struct MotCtrl *)sub->motReq)->cliffHeight);
                                    if (debug_font_flag & 1) {
                                        debug_Printf(30, 170, 0xFFFFFFF, "wattr = [%x]\n",
                                                     GOBJ_SUB(self)->ctrl.wallAttr);
                                        if (debug_font_flag & 1) {
                                            debug_Printf(30, 180, 0xFFFFFFF, "bttype= [%d]\n",
                                                         GOBJ_ACT(self)->enemy->battleType);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

void ACTSendMailCorrect(GObj *self, int mail)
{
    Act *s = GOBJ_ACT(self);
    if ((mail == 0xB5 || mail == 0xBA) && self->kind == 1) {
        long long f = (long long)s->wish2.ll;
        if (((int)(f >> 5) & 1) && ((int)((long long)s->wish4.ll >> 5) & 1)) {
            mail = 0xB6;
        } else if ((int)(f >> 3) & 1) {
            mail = ((int)((long long)s->wish4.ll >> 3) & 1) ? 0xB7 : mail;
        }
    }
    iosOmSendMail(self, mail, self);
}

void _ACTCommonMailTest(GObj *self, int stopCnt, int walkCnt, int runCnt)
{
    Act *s;
    int ret = 0;
    unsigned int m;

    s = GOBJ_ACT(self);
    if (self == boyGObj) {
        if (optionControlType == 1 ? (s->pad.trg & 8) != 0 : (s->pad.now & 8) == 0) {
            handoff_heroin();
        }
    }
    m = (unsigned int)s->actMode;
    switch (m) {
    case 1:
    case 2:
    case 3:
    case 14:
    case 79:
    case 116:
        ret = 1;
        break;
    case 36:
        if (self == boyGObj || self == girlGObj) {
            ret = 1;
        }
        break;
    case 29:
        ret = self == girlGObj;
        break;
    case 15:
    case 20:
    case 21:
        ret = self == boyGObj;
        break;
    }
    if (((motionKind + GOBJ_SUB(self)->ctrl.motion)->flags2.word >> 1) & 1) {
        ret = 1;
    }
    if (ret) {
        if (0.1f < s->stick.mag && (unsigned int)(s->stick.angle + 45) < 91) {
            ACTSendMailCorrect(self, 0x14C);
        }
        if (0.1f < s->stick.mag && !((unsigned int)(s->stick.angle + 134) < 269) &&
            !(s->actMode == 3 && ((int)(s->flags18.ll >> 44) & 1))) {
            ACTSendMailCorrect(self, 0x14D);
            if (((int)(s->flags20.ll >> 18) & 1) && !((int)(s->flags18.ll >> 44) & 1)) {
                ACTSendMailCorrect(self, 0xBC);
            }
        }
        if (0.1f < s->stick.mag && (unsigned int)(s->stick.angle - 46) < 89) {
            ACTSendMailCorrect(self, 0x14E);
        }
        if (0.1f < s->stick.mag && !(s->stick.angle < -134) && s->stick.angle < -45) {
            ACTSendMailCorrect(self, 0x14F);
        }
        if (0.1f < s->stick.mag && (s->stick.mag < 0.99f || (s->pad.now & 0x20)) &&
            !(walkCnt < 4)) {
            ACTSendMailCorrect(self, 0xB5);
        }
        /* the negated conjunct is the 0xB5 guard's whole predicate, repeated;
           it emits a real (dead) branch, so it is in the shipped code. */
        if (0.1f < s->stick.mag &&
            !(0.1f < s->stick.mag && (s->stick.mag < 0.99f || (s->pad.now & 0x20))) &&
            !(runCnt < 4)) {
            ACTSendMailCorrect(self, 0xBA);
        }
        if (!(0.1f < s->stick.mag) && 0 < stopCnt) {
            ACTSendMailCorrect(self, 0xC7);
        }
    }
    if (s->actMode == 73) {
        if (0.1f < s->stick.mag && (s->stick.mag < 0.99f || (s->pad.now & 0x20)) &&
            !(walkCnt < 4)) {
            ACTSendMailCorrect(self, 0xB5);
        }
        /* the negated conjunct is the 0xB5 guard's whole predicate, repeated;
           it emits a real (dead) branch, so it is in the shipped code. */
        if (0.1f < s->stick.mag &&
            !(0.1f < s->stick.mag && (s->stick.mag < 0.99f || (s->pad.now & 0x20))) &&
            !(runCnt < 4)) {
            ACTSendMailCorrect(self, 0xBA);
        }
    }
}

int E3_LeverCheck(GObj *self)
{
    float buf[3];
    buf[0] = *(float *)((char *)test_CURRENTORIENT(GOBJ_SUB(self)->root.wall.o.obj) + 0x0);
    buf[1] = *(float *)((char *)test_CURRENTORIENT(GOBJ_SUB(self)->root.wall.o.obj) + 0x4);
    buf[2] = *(float *)((char *)test_CURRENTORIENT(GOBJ_SUB(self)->root.wall.o.obj) + 0x8);
    _ApplyRyGV(buf, -1.5707964f);
    return _RotyGV(test_CURRENTORIENT(self), buf) < 0 ? -_RotyGV(test_CURRENTORIENT(self), buf) < 45
                                                      : _RotyGV(test_CURRENTORIENT(self), buf) < 45;
}

extern SlowrunRec motionOrient[];

typedef struct { /* field names derived */
    char pad0[396];
    unsigned char f18C;
    char pad18D[7];
} CarryMot; /* derived name */

static __inline__ unsigned char requestBecarryMotion(GObj *self, int mot, int wait,
                                                     int n) /* derived name */
{
    Act *sub = GOBJ_ACT(self);
    const BecPair *tbl = pairMotion;
    const BecPair *end = tbl + 31;
    const BecPair *p;
    int save0;
    int save1;

    for (p = tbl; p != end; p++) {
        if (p->mot == mot) {
            break;
        }
    }
    if (p != end) {
        save0 = motionOrient[n].w[2];
        save1 = motionOrient[n].w[4];
        motionOrient[n].w[2] = p->req;
        motionOrient[n].w[4] = wait;
        SetMotionRequest(self, 268, *(MotOriReq *)&sub->env.motOriReq);
        motionOrient[n].w[2] = save0;
        motionOrient[n].w[4] = save1;
        return 1;
    }
    return 0;
}

void actCommonBecarry(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    int cur = -1;
    GObj *g;
    int old;
    unsigned char done;
    unsigned long long fl;

    g = s->carrier;
    ACTGameCollisionOff((volatile int *)self);
    ACT_AFTER_PROC(s) = afterCommonBecarry;
    s->flags18.ll &= ~(1ULL << 46);
    _ACTWait(1);
    while (1) {
        _ACTCharStatus_Set(self, 9, -1.0f, 0);
        old = cur;
        cur = GOBJ_SUB(g)->ctrl.motion;
        if (old != cur) {
            done = requestBecarryMotion(self, cur, (GOBJ_ACT(g)->actMode == 103) ? 30 : 0, 2118);
            if (!done) {
                s->flags18.ll |= (1ULL << 46);
            }
        }
        if (6 <= s->modeFrame) {
            if (actModeTbl[GOBJ_ACT(g)->actMode].carried ||
                (fl = ((CarryMot *)&motionKind[GOBJ_SUB(g)->ctrl.motion])->f18C, fl >> 7)) {
                afterCommonCarry(g);
                debug_StdPrintfDummy("girl becarry error");
            }
        }
        if (actModeTbl[GOBJ_ACT(boyGObj)->actMode].keepHand == 0) {
            ACTGame_DisconnectHand();
        }
        if (debug_font_flag & 1) {
            debug_Printf(100, 150, 0xFFFFFFF, "[%s]\n",
                         (motionKind + GOBJ_SUB(girlGObj)->ctrl.motion)->name);
        }
        if (debug_font_flag & 1) {
            debug_Printf(
                100, 160, 0xFFFFFFF, "[%s]\n",
                motionKind[ICO_RAW(int, *(char **)(ICO_RAW(char *, s, 0x144, s->carrier) + 0x15C),
                                   0x4A0, GOBJ_SUB(s->carrier)->ctrl.motion)]
                    .name);
        }
        if (debug_font_flag & 1) {
            debug_Printf(
                100, 170, 0xFFFFFFF, "[%s]\n",
                actModeTbl[ICO_RAW(int, *(char **)(ICO_RAW(char *, s, 0x144, s->carrier) + 0x164),
                                   0x34, GOBJ_ACT(s->carrier)->actMode)]
                    .name);
        }
        _ACTWait(1);
    }
}

static inline void SetIdleMotionRange(int k, int mot, int mot2) /* derived name */
{
    int n1 = actDataTbl[k].orientRow;
    int n2 = actDataTbl[k].orientRow2;
    int n3 = actDataTbl[k].orientFirst;
    int n4 = actDataTbl[k].orientEnd;
    int i;

    if (mot != 1147 && mot2 != 1147) {
        motionOrient[n1].w[2] = mot;
        motionOrient[n2].w[2] = mot2;
        for (i = n3; i < n4; i++) {
            motionOrient[i].w[0] = mot;
        }
    }
}

void subCommonIdle(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    int k = s->actKind;
    int m0 = actDataTbl[k].idleMotion;
    int timer = 0;
    int same = 0;
    int sameHand = 0;
    int i;
    int j;
    int base;
    int lim;
    int n;
    int cur;
    int idx;
    int found;
    int nxt;

    for (i = 0; i < 9; i++) {
        if (idlingDef[i].motion[k] == m0) {
            same++;
        } else {
            break;
        }
    }
    for (i = 9; i < 10; i++) {
        if (idlingDef[i].motion[k] == m0) {
            sameHand++;
        } else {
            break;
        }
    }
    while (1) {
        cur = GOBJ_SUB(self)->ctrl.motion;
        if ((self == boyGObj && scpBoyControlReadDisable != 0) ||
            GOBJ_WORK(self)->noInterpTimer > 0 || ((int)(s->flags20.ll >> 44) & 1)) {
            timer = 0;
            ACTSendMailCorrect(self, 180);
        } else if (s->actMode != 1 && s->actMode != 69) {
            timer = 0;
        } else if ((int)(s->flags18.ll >> 62) & 1) {
            timer = 0;
            ACTSendMailCorrect(self, 180);
        } else {
            if (self == boyGObj && ACTGame_FLAG_TETSUNAGI()) {
                base = 9;
                lim = 10;
                n = sameHand;
            } else {
                base = 0;
                lim = 9;
                n = same;
            }
            if (cur == m0) {
                if (!((s->flags20.ll >> 45) & 1)) {
                    timer = timer + 1;
                }
                if (n > 0) {
                    if (((60 - systemStatus[0] * 10) / systemStatus[1]) * 350 / 60 * (n + 1) <
                        timer) {
                        idx = base + n;
                        if (idx >= lim || idlingDef[idx].motion[k] == 1147) {
                            idx = base;
                        }
                        SetIdleMotionRange(k, idlingDef[idx].motion[k], idlingDef[idx].motion[k]);
                        ACTSendMailCorrect(self, 179);
                    }
                } else {
                    if (((60 - systemStatus[0] * 10) / systemStatus[1]) * 3 < timer) {
                        SetIdleMotionRange(k, idlingDef[base].motion[k], idlingDef[base].motion[k]);
                        ACTSendMailCorrect(self, 179);
                    }
                }
            } else {
                found = -1;
                timer = 0;
                for (j = base; j < lim; j++) {
                    if (cur == idlingDef[j].motion[k]) {
                        found = j;
                    }
                }
                if (found < 0) {
                    timer = 0;
                } else {
                    if (found + 1 < lim) {
                        nxt = idlingDef[found + 1].motion[k];
                    } else {
                        nxt = m0;
                    }
                    if (nxt == 1147) {
                        nxt = m0;
                    }
                    if (self == girlGObj && nxt == m0) {
                        nxt = idlingDef[0].motion[k];
                    }
                    SetIdleMotionRange(k, idlingDef[found].motion[k], nxt);
                    ACTSendMailCorrect(self, 179);
                }
            }
        }
        _ACTWait(1);
    }
}

void ContinueCorrectPosition(void *obj)
{
    float v[4];

    (GOBJ_ACT(obj)->enemy->corrCount)++;
    if (GOBJ_ACT(obj)->enemy->corrCount <= GOBJ_ACT(obj)->enemy->corrFrames) {
        if (obj == (char *)girlGObj) {
            if (debug_font_flag & 1) {
                debug_Printf(100, 100, 0xFFFFFFF, "timer=%2d/%2d\n",
                             GOBJ_ACT(obj)->enemy->corrCount, GOBJ_ACT(obj)->enemy->corrFrames);
            }
        }
        _InterGV(v, (void *)&GOBJ_ACT(obj)->enemy->corrPosX,
                 (void *)&GOBJ_ACT(obj)->enemy->corrDstX, (float)GOBJ_ACT(obj)->enemy->corrCount,
                 (float)(GOBJ_ACT(obj)->enemy->corrFrames - GOBJ_ACT(obj)->enemy->corrCount));
        SetDirectRootPositionNoFitting(obj, v);
        if (GOBJ_ACT(obj)->enemy->corrMode == 1) {
            _InterGV(v, (void *)&GOBJ_ACT(obj)->enemy->corrDirX,
                     (void *)&GOBJ_ACT(obj)->enemy->corrDstDirX,
                     (float)GOBJ_ACT(obj)->enemy->corrCount,
                     (float)(GOBJ_ACT(obj)->enemy->corrFrames - GOBJ_ACT(obj)->enemy->corrCount));
            sceVu0Normalize(v, v);
            SetMotionDirection(obj, v);
        }
    } else {
        *(long long *)&GOBJ_ACT(obj)->enemy->corrMode &= 0xFFFFFFFEFFFFFFFFLL;
    }
}

void actCommonTurn(GObj *volatile self)
{
    float q[4];
    float o[4];
    Act *s = GOBJ_ACT(self);
    float *t = s->env.turnDir.f;
    int d;

    while (1) {
        GetRootMotionOrient(q, self);
        d = _RotyGV(s->env.turnDir.f, q);
        debug_Arrow(100.0f, test_CURRENTROOT((void *)self), s->env.turnDir.f, 0, 0, 0xFF);
        if (self == girlGObj) {
            GetSkeltonOrient(o, self, 1);
            if (_AbsRotyGV(t, o) < 60) {
                if (s->stick.mag != 0.0f) {
                    ACTSendMailCorrect(self, 0xF0);
                }
                ACTSendMailCorrect(self, 0xF1);
            }
        } else {
            if ((d < 0 ? -d : d) < 15) {
                ACTSendMailCorrect(self, 0xF1);
            }
        }
        switch ((unsigned int)s->intrKind) {
        case 0xE7:
        case 0xE9:
            GOBJ_WORK(self)->turnTimer = (60 - systemStatus[0] * 10) / systemStatus[1];
            break;
        case 0xE8:
        case 0xEA:
            GOBJ_WORK(self)->turnTimer2 = (60 - systemStatus[0] * 10) / systemStatus[1];
            break;
        }
        _ACTWait(1);
    }
}

void actCommonBackhand(GObj *volatile self)
{
    float v[4];
    float dir[4];
    float pos[4];
    int frame;

    _OrientXZGV(dir, test_CURRENTROOT((void *)self), test_CURRENTROOT(girlGObj));
    SetMotionDirection(self, dir);
    v[0] = test_CURRENTORIENT(girlGObj)[0];
    v[1] = test_CURRENTORIENT(girlGObj)[1];
    v[2] = test_CURRENTORIENT(girlGObj)[2];
    while (1) {
        sceVu0ScaleVector(pos, v, 50.0f);
        sceVu0AddVector(pos, test_CURRENTROOT(girlGObj), pos);
        SetDirectRootPositionNoFittingWithNodePointXZ((void *)self, 6, pos, 0.2f);
        if (GOBJ_SUB(self)->ctrl.motion == 0xBD) {
            _OrientXZGV(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT((void *)self));
            SetMotionDirection(self, dir);
        }
        /* the frame budget is computed and dropped */
        frame = (60 - systemStatus[0] * 10) / systemStatus[1];
        _ACTWait(1);
    }
}

typedef union { /* field names derived */
    int i;
    float f;
} IntFloatSR; /* derived name */

void actCommonSlowrun(GObj *volatile self)
{
    float p[2][4];

    while (1) {
        int i1;
        int i2;

        i1 = GetSkeltonFocusNode(girlGObj, 22);
        i2 = GetSkeltonFocusNode(boyGObj, 6);
        ((IntFloatSR *)p[0])[0].f = *(float *)((i1 << 6) + GOBJ_SUB(girlGObj)->nodeMtx + 0x30);
        ((IntFloatSR *)p[0])[1].f = *(float *)((i1 << 6) + GOBJ_SUB(girlGObj)->nodeMtx + 0x34);
        ((IntFloatSR *)p[0])[2].f = *(float *)((i1 << 6) + GOBJ_SUB(girlGObj)->nodeMtx + 0x38);
        ((IntFloatSR *)p[1])[0].f = *(float *)((i2 << 6) + GOBJ_SUB(boyGObj)->nodeMtx + 0x30);
        ((IntFloatSR *)p[1])[1].f = *(float *)((i2 << 6) + GOBJ_SUB(boyGObj)->nodeMtx + 0x34);
        ((IntFloatSR *)p[1])[2].f = *(float *)((i2 << 6) + GOBJ_SUB(boyGObj)->nodeMtx + 0x38);
        if (((motionKind + GOBJ_SUB(girlGObj)->ctrl.motion)->flags.word >> 29) & 1) {
            SetDirectRootPositionNoFittingWithNodePointXZ((void *)self, 6, p[0], 0.2f);
        } else {
            SetDirectRootPositionNoFittingWithNodePointXZ((void *)self, 6, p[0], 0.1f);
        }
        _ACTWait(1);
    }
}

typedef struct { /* field names derived */
    char pad0[20];
    int f14;
} TruckLeverWork; /* derived name */

void actCommonTruckLever(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    GObj *lev = s->env.pullObj;

    s->after = (void *)afterCommonTruckLever;
    actMotDirToWall(self);
    while (1) {
        if (lev != 0) {
            if (GOBJ_SUB(self)->ctrl.frameFlag2 != 0) {
                correctLeverHoldPoint((void *)self, lev);
            }
            if (GOBJ_SUB(self)->ctrl.frameFlag1 != 0) {
                lever_nego1((void *)self, lev);
            }
        }
        _ACTWait(1);
    }
}

void ACT_LAYOUT_GAMEOVER(void)
{
    if (gameover_layout_flag == 0) {
        gameover_layout_flag = 1;
        ico_gs_signal(ICO_GS_EV_GAME_OVER, 0);
        lt_switch_layout(62);
    }
}

void ACTAdjustPlane(GObj *self, void *wall)
{
    AdjustRootPositionToVerticalSidePlaneOfWall(self, wall, 30.0f);
}

inline void ACTAcceptMail(GObj *self, int mail)
{
    if (mail == 0xB1) {
        GOBJ_WORK(self)->mailB1Timer = ((60 - systemStatus[0] * 10) / systemStatus[1]) * 10;
    }
}

inline int _ACTMotDirSmzDirect(GObj *self, float *dir)
{
    Act *s = GOBJ_ACT(self);

    s->dir[0] = dir[0];
    s->dir[1] = dir[1];
    s->dir[2] = dir[2];
    return SetMotionDirectionSmooze(
        self, dir,
        (float)(self == girlGObj && girlControlMode != 0
                    ? (motionKind + GOBJ_SUB(self)->ctrl.motion)->girlDirFrames
                    : (motionKind + GOBJ_SUB(self)->ctrl.motion)->dirFrames));
}

inline void WithMailFunc_Idling(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    int k = s->actKind;
    int mot = GOBJ_SUB(self)->ctrl.motion;

    SetIdleMotionRange(k, mot, mot);
}

inline void WithMailFunc_BossDamaged(GObj *self)
{
    EnemyBattleWork *m = GOBJ_ACT(self)->enemy;
    m->bossLife -= 1;
}

inline void WithMailFunc_FallDead(GObj *self)
{
    float v[4];
    Sub15C *s = GOBJ_SUB(self);
    v[0] = s->root.plane.f[0];
    v[1] = s->root.plane.f[1];
    v[2] = s->root.plane.f[2];
    sceVu0Normalize(v, v);
    if (ico_dcmp(ico_f2d(FSqrt(v[0] * v[0] + v[2] * v[2])), ICO_D(0.3)) > 0) {
        v[1] = 0.0f;
        sceVu0Normalize(v, v);
        SetMotionDirection(self, v);
    }
}

inline void actCommonReviveAir(GObj *volatile self)
{
    SetDirectRootPositionNoFitting((void *)self, (char *)&GOBJ_ACT(self)->restartPosX);
    EnemySetfAppearAll((void *)self);
    for (;;) {
        ACTSendMailCorrect(self, 0xE2);
        _ACTWait(1);
    }
}

inline void actCommonOne(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);

    if (s->intrKind == 416) {
        if (*(int *)&s->soundFlag & 1) {
            s->soundWait = ((60 - systemStatus[0] * 10) / systemStatus[1]) * 5;
        }
    }
    for (;;) {
        ACTSendMailCorrect(self, 0xC7);
        _ACTWait(1);
    }
}

inline void actCommonDelete(GObj *volatile self)
{
    _ACTWait(0);
}

inline void actCommonCatchFire(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    int lit = 0;

    SetMotionDirection(self, s->env.torchOrient);
    for (;;) {
        if (GetMotionFrameFlag1((void *)self) && !lit) {
            LightTorchOnOfWeapon(s->weapon);
            lit = 1;
        }
        ACTSendMailCorrect(self, 0xC7);
        _ACTWait(1);
    }
}

inline void actCommonCatchFireBomb(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    int lit = 0;

    SetMotionDirection(self, s->env.torchOrient);
    for (;;) {
        if (GetMotionFrameFlag1((void *)self) && !lit) {
            LightTorchOn(s->env.bombObj);
            lit = 1;
        }
        ACTSendMailCorrect(self, 0xC7);
        _ACTWait(1);
    }
}

inline void actCommonPutFire(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    int lit = 0;

    SetMotionDirection(self, s->env.torchRevOrient);
    for (;;) {
        if (GetMotionFrameFlag1((void *)self) && !lit) {
            LightTorchOn(s->env.torchRevObj);
            lit = 1;
        }
        ACTSendMailCorrect(self, 0xC7);
        _ACTWait(1);
    }
}

inline void actCommonBoxReverbe(GObj *volatile self)
{
    _ACTWait(40);
    for (;;) {
        ACTSendMailCorrect(self, 0xC7);
        _ACTWait(1);
    }
}

inline void actCommonItem(GObj *volatile self)
{
    for (;;) {
        ACTSendMailCorrect(self, 0xC7);
        _ACTWait(1);
    }
}

inline void actCommonClimb(GObj *volatile self)
{
    ACTAdjustPlane(self, &GOBJ_WORK(self)->intrReq.a.wall);
    for (;;) {
        ACTSendMailCorrect(self, 0xC7);
        _ACTWait(1);
    }
}

typedef struct { /* field names derived */
    float x, y, z;
} Vec3f; /* derived name */

inline void actCommonLadderBellow(GObj *volatile self)
{
    WallCfg hit;
    float p[4];
    float q[4];
    float dir[4];
    int attr[4];

    while (1) {
        GetSkeltonPosition(p, self, 0x2C);
        sceVu0ScaleVector(dir, test_CURRENTORIENT(self), 50.0f);
        sceVu0AddVector(q, p, dir);
        if (ACTCheckCollis_CI(p, q, attr, &hit) != 0) {
            if (CompareAttribute(attr[0], 0x3000) != 0) {
                ACTSendMailCorrect(self, 0x91);
                GOBJ_WORK(self)->bellowWall3000.wall = hit;
            }
            if (CompareAttribute(attr[0], 0x400) != 0) {
                ACTSendMailCorrect(self, 0x90);
                GOBJ_WORK(self)->bellowWall400.wall = hit;
            }
        }
        ACTSendMailCorrect(self, 0xE2);
        _ACTWait(1);
    }
}

inline void actCommonLadderBellowHang(GObj *volatile self)
{
    for (;;) {
        if (!CheckWallAttribute((void *)self, 0x3000) && !CheckWallAttribute((void *)self, 0x400)) {
            ACTSendMailCorrect(self, 0xE2);
        }
        _ACTWait(1);
    }
}

inline void actCommonEdge(GObj *volatile self)
{
    for (;;) {
        ACTSendMailCorrect(self, 0x150);
        if (-GOBJ_SUB(self)->ctrl.cliffDepth > 250.0f) {
            ACTSendMailCorrect(self, 0x18);
        }
        _ACTWait(1);
    }
}

inline void actCommonDodgeJump(GObj *volatile self)
{
    float buf[4];
    ICO_WORD id = 0;
    Act *s = GOBJ_ACT(self);

    if (self->kind == 1) {
        ACTSearchEnemy((void *)self, &id, buf);
    } else {
        _OrientXZGV(s->dir, test_CURRENTROOT(boyGObj), test_CURRENTROOT((void *)self));
        SetMotionDirection(self, s->dir);
    }
    for (;;) {
        _ACTWait(1);
    }
}

inline void actCommonFallDamage(GObj *volatile self)
{
    if (self == boyGObj) {
        brainAddLevelGirl(1000.0f);
    }
    for (;;) {
        if (self == boyGObj && girlGObj != 0) {
            brainSetSpMode();
            iosOmSendMail(girlGObj, 0x3D, self);
        }
        ACTSendMailCorrect(self, 0xC7);
        _ACTWait(1);
    }
}

inline void actCommonLever2(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    GObj *lev = s->env.pullObj;

    SetDirectRootPositionXZ((void *)self, s->env.pullPos);
    actMotDirToWall(self);
    while (1) {
        if (lev != 0) {
            /* an empty guard: the frame flag is re-read and nothing is done */
            if (GOBJ_SUB(self)->ctrl.frameFlag2 != 0) {}
            if (GOBJ_SUB(self)->ctrl.frameFlag1 != 0) {
                lever_nego1((void *)self, lev);
            }
        }
        _ACTWait(1);
    }
}

inline void actCommonRopeTouchWall(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);

    while (1) {
        if (((60 - systemStatus[0] * 10) / systemStatus[1]) / 2 < s->modeFrame) {
            ACTSendMailCorrect(self, 0xAA);
        }
        _ACTWait(1);
    }
}

typedef struct { /* field names derived */
    char pad0[24];
    int f18;
} RopeSwingWork; /* derived name */

inline void actCommonRopeSwing(GObj *volatile self)
{
    float q[4];
    Act *s = GOBJ_ACT(self);

    LockChainGeo(s->chain);
    ACT_AFTER_PROC(s) = (void (*)(GObj *))actAfterForceRopeSwing;
    GetCorrectOrientOfChain(q, (void *)self);
    s->ropeSwingX = q[0];
    s->ropeSwingY = q[1];
    s->ropeSwingZ = q[2];
    PlumbOrientUpdateChain(s->chain, q);
    _ACTWait(1);
    while (1) {
        switch (CollisCheckInRope((void *)self, s->chain)) {
        case 1:
            ACTSendMailCorrect(self, 0xA8);
            break;
        case 2:
            ACTSendMailCorrect(self, 0xA7);
            break;
        }
        UnLockChainGeo(s->chain);
        ChainGeo(s->chain);
        LockChainGeo(s->chain);
        SetMotionDirection(self, q);
        _ACTWait(1);
    }
}

inline void actCommonRopeTurn(GObj *volatile self)
{
    for (;;) {
        _ACTWait(1);
    }
}

static inline int isRopeDownEndOnFloor(GObj *self) /* derived name */
{
    ClipWork work;
    MotionDef *rec = &motionKind[GOBJ_SUB(self)->ctrl.motion];

    if ((rec->flags.word >> 4) & 1) {
        GetSkeltonPosition(work.pt[0], self, 0x2C);
        GetSkeltonPosition(work.pt[1], self, 0x33);
        work.pt[1][1] = work.pt[1][1] - 5.0f;
        ClipFloor(&work);
        if (work.floor.elem != 0) {
            return 1;
        }
    }
    return 0;
}

inline void actCommonRopeDownEnd(GObj *volatile self)
{
    while (1) {
        if (isRopeDownEndOnFloor(self)) {
            ACTSendMailCorrect(self, 0xA9);
        }
        _ACTWait(1);
    }
}

typedef struct { /* field names derived */
    char pad0[304];
    int f130;
    int f134;
    int f138;
} RopeJumpWork; /* derived name */

inline void actCommonRopeJump(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);

    ACT_AFTER_PROC(s) = (void (*)(GObj *))actAfterRopeJump;
    if (s->intrKind == 167) {
        GOBJ_SUB(self)->root.move[0] = 0;
        GOBJ_SUB(self)->root.move[1] = 0;
        GOBJ_SUB(self)->root.move[2] = 0;
    }
    for (;;) {
        ACTSendMailCorrect(self, 0xBD);
        _ACTWait(1);
    }
}

inline void actCommonRopeJumpBefore(GObj *volatile self)
{
    for (;;) {
        ACTSendMailCorrect(self, 0xBD);
        _ACTWait(1);
    }
}

typedef struct { /* field names derived */
    char pad0[24];
    int f18;
} RopeTurnSpWork; /* derived name */

inline void actCommonRopeTurnSpecial(GObj *volatile self)
{
    Vec4u base;
    float p0[4];
    float p1[4];
    float out[4];

    ACT_AFTER_PROC(GOBJ_ACT(self)) = (void (*)(GObj *))afterCommonRopeTurnSpecial;
    base.f[0] = GOBJ_SUB(self)->root.holdPoint[0];
    base.f[1] = GOBJ_SUB(self)->root.holdPoint[1];
    base.f[2] = GOBJ_SUB(self)->root.holdPoint[2];
    while (1) {
        GetCageChainPoint(p0, p1, GOBJ_WORK(self)->ropeCage);
        _InterGV(out, p0, p1, base.f[1] - p0[1], p1[1] - base.f[1]);
        out[1] = base.f[1];
        SetChainRootUpdateMode(self, 3, out);
        _ACTWait(1);
    }
}

inline void actCommonRopeClimbEnd2(GObj *volatile self)
{
    for (;;) {
        ACTSendMailCorrect(self, 0xC7);
        _ACTWait(1);
    }
}

inline void actCommonCornered(GObj *volatile self)
{
    float v[4];

    sceVu0ScaleVector(v, test_CURRENTORIENT(self), -1.0f);
    SetMotionDirection(self, v);
    for (;;) {
        _ACTCharStatus_Set(self, 3, -1.0f, 0);
        _ACTWait(1);
    }
}

inline void actCommonLookaround(GObj *volatile self)
{
    for (;;) {
        ACTSendMailCorrect(self, 0xC7);
        _ACTWait(1);
    }
}

inline void actCommonTurnWarn(GObj *volatile self)
{
    float q[4];
    Act *s = GOBJ_ACT(self);
    int prev = s->curMot;

    while (1) {
        int d;

        if (s->curMot != 0x10D) {
            prev = s->curMot;
        } else {
            s->motReq = SetMotionRequest((void *)self, prev, s->env.motOriReq);
        }
        GetRootMotionOrient(q, self);
        d = _RotyGV(&GOBJ_WORK(self)->hideDirX, q);
        if ((d < 0 ? -d : d) < 0xF) {
            ACTSendMailCorrect(self, 0xF5);
        }
        _ACTWait(1);
    }
}

inline void actCommonTurnStrict(GObj *volatile self)
{
    float q[4];
    float *t = GOBJ_ACT(self)->env.turnDir.f;

    for (;;) {
        int d;

        GetRootMotionOrient(q, self);
        d = _RotyGV(t, q);
        if ((d < 0 ? -d : d) < 0xF) {
            ACTSendMailCorrect(self, 0xF4);
        }
        _ACTWait(1);
    }
}

inline void actCommonPPipe(GObj *volatile self)
{
    _ACTWait(0);
}

inline void actCommonHandrail(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);

    GOBJ_WORK(self)->handrailOrient[0] = s->env.wallOrient[0];
    GOBJ_WORK(self)->handrailOrient[1] = s->env.wallOrient[1];
    GOBJ_WORK(self)->handrailOrient[2] = s->env.wallOrient[2];
    for (;;) {
        _ACTWait(1);
    }
}

inline void actCommonOneWall(GObj *volatile self)
{
    GOBJ_ACT(self)->after = (void *)afterCommonOneWall;
    _ACTWait(0);
}

inline void motCommonBoxPush(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);

    s->pushDir = 1;
    while (1) {
        if (IsThisBoxTruck(s->box) == 0) {
            SetMotionDirection(self, GOBJ_WORK(self)->boxDir);
        } else {
            actMotDirToWall(self);
        }
        _ACTWait(1);
    }
}

inline void motCommonBoxPull(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);

    s->pushDir = -1;
    while (1) {
        if (IsThisBoxTruck(s->box) == 0) {
            SetMotionDirection(self, GOBJ_WORK(self)->boxDir);
        } else {
            actMotDirToWall(self);
        }
        _ACTWait(1);
    }
}

inline void motCommonBarPush(GObj *volatile self)
{
    GOBJ_ACT(self)->pushDir = 1;
    _ACTWait(0);
}

inline void motCommonBarPull(GObj *volatile self)
{
    char *g = (char *)self;
    GOBJ_ACT(g)->pushDir = 0xFFFFFFFFu;
    _ACTWait(0);
}

typedef struct { /* field names derived */
    char pad0[56];
    int f38;
} LadderMotWork; /* derived name */

inline void motCommonLadderUp(GObj *volatile self)
{
    LadderMotWork *s = (LadderMotWork *)(char *)GOBJ_ACT(self);

    while (1) {
        GOBJ_ACT(self)->pushDir = 1; /* LadderMotWork's 0x38 */
        switch (GOBJ_ACT(self)->enemy->ladderUpStep) {
        case 1:
            ACTSendMailCorrect(self, 0x89);
        case 2:
            ACTSendMailCorrect(self, 0x8A);
            break;
        }
        _ACTWait(1);
    }
}

typedef struct { /* field names derived */
    char pad0[56];
    unsigned int f38;
} LadderDownMotWork; /* derived name */

inline void motCommonLadderDown(GObj *volatile self)
{
    LadderDownMotWork *s = (LadderDownMotWork *)(char *)GOBJ_ACT(self);

    while (1) {
        GOBJ_ACT(self)->pushDir = -1; /* LadderMotWork's 0x38 */
        switch (GOBJ_ACT(self)->enemy->ladderDownStep) {
        case 1:
            ACTSendMailCorrect(self, 0x89);
        case 2:
            ACTSendMailCorrect(self, 0x8A);
            break;
        }
        _ACTWait(1);
    }
}

inline void motCommonSlip(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);

    while (1) {
        long long f = (long long)s->wish1.ll;

        if (!((((int)(f >> 16) & 1) && ((int)((long long)s->wish3.ll >> 16) & 1)) ||
              (((int)(f >> 17) & 1) && ((int)((long long)s->wish3.ll >> 17) & 1)))) {
            ACTSendMailCorrect(self, 0xC7);
        }
        _ACTWait(1);
    }
}

inline void motCommonRopejumpDircorrect(GObj *volatile self)
{
    float v[4];
    Act *s = GOBJ_ACT(self);

    v[0] = s->ropeSwingX;
    v[1] = s->ropeSwingY;
    v[2] = s->ropeSwingZ;
    if (s->intrKind == 190) {
        sceVu0ScaleVector(v, v, -1.0f);
    }
    for (;;) {
        SetMotionDirection(self, v);
        _ACTWait(1);
    }
}

inline void motCommonRopeTurnSpecialR(GObj *volatile self)
{
    int i = 0;
    int base = (int)(_GetDirection(test_CURRENTORIENT(self)) / 3.1415927f * 180.0f);
    Act *s = GOBJ_ACT(self);
    float dir[4];
    int wait = 18, deg = 0;

    while (1) {
        memset(dir, 0, 16);
        dir[2] = 1.0f;
        _ApplyRyGV(dir, (float)RoundDegGV(base + deg) * 3.1415927f / 180.0f);
        debug_Arrow(200.0f, test_CURRENTROOT((void *)self), dir, 0xFF, 0, 0xFF);
        SetMotionDirection(self, dir);
        if (i++ % wait == 0 && !(0.1f < s->stick.mag)) {
            ACTSendMailCorrect(self, 0xAC);
        }
        deg += 5;
        _ACTWait(1);
    }
}

inline void motCommonRopeTurnSpecialL(GObj *volatile self)
{
    int i = 0;
    int base = (int)(_GetDirection(test_CURRENTORIENT(self)) / 3.1415927f * 180.0f);
    Act *s = GOBJ_ACT(self);
    float dir[4];
    int wait = 18, deg = 0;

    while (1) {
        memset(dir, 0, 16);
        dir[2] = 1.0f;
        _ApplyRyGV(dir, (float)RoundDegGV(base - deg) * 3.1415927f / 180.0f);
        debug_Arrow(200.0f, test_CURRENTROOT((void *)self), dir, 0xFF, 0, 0xFF);
        SetMotionDirection(self, dir);
        if (i++ % wait == 0 && !(0.1f < s->stick.mag)) {
            ACTSendMailCorrect(self, 0xAC);
        }
        deg += 5;
        _ACTWait(1);
    }
}

inline void motCommonTruckLeverLoop(GObj *volatile self)
{
    GObj *sw = GOBJ_ACT(self)->env.pullObj;

    _ACTWait(6);
    SetSwitchState(sw, 0);
    debug_StdPrintfDummy("loop");
    _ACTWait(0);
}

inline void motCommonTruckLeverPull(GObj *volatile self)
{
    GObj *sw = GOBJ_ACT(self)->env.pullObj;
    _ACTWait(30);
    SetSwitchState(sw, -1);
    debug_StdPrintfDummy("pull");
    _ACTWait(0);
}

inline void motCommonTruckLeverPush(GObj *volatile self)
{
    GObj *sw = GOBJ_ACT(self)->env.pullObj;
    _ACTWait(30);
    SetSwitchState(sw, 1);
    debug_StdPrintfDummy("push");
    _ACTWait(0);
}

inline void funcCommonRopeBefore(GObj *self, int mail, GObj *chain)
{
    GOBJ_ACT(self)->chain = chain;
}

inline void extraCommonNull(GObj *volatile self)
{
    for (;;) {
        _ACTWait(1);
    }
}

inline void extraCommonCall(GObj *volatile self)
{
    if (girlGObj) {
        iosOmSendMail(girlGObj, 0x44, isysCurrentGObj);
    }
    for (;;) {
        _ACTWait(1);
    }
}

inline void funcCommonWayOn(GObj *self)
{
    if (self == girlGObj) {
        girlcalled = 1;
    }
}

inline void funcCommonSofaWakeup(GObj *self)
{
    GOBJ_ACT(self)->enemy->sofaWake = 0;
}

inline int _ACTMotReqResult(GObj *self, int mot)
{
    Act *s = GOBJ_ACT(self);
    char *r = SetMotionRequest(self, mot, s->env.motOriReq);
    s->motReq = r;
    return ((struct MotCtrl *)r)->shifted != 0;
}

/* StartCorrectPosition and IsCorrectPosition precede test_CURRENTORIENT and
   test_CURRENTROOT, so their calls to the two accessors stay calls. */
typedef union { /* field names derived */
    unsigned long long ll;
    int i;
} CorrFlag; /* derived name */

inline void StartCorrectPosition(GObj *self, float *pos, float *dir, int mode, float t)
{
    GOBJ_ACT(self)->enemy->corrPosX = test_CURRENTROOT(self)[0];
    GOBJ_ACT(self)->enemy->corrPosY = test_CURRENTROOT(self)[1];
    GOBJ_ACT(self)->enemy->corrPosZ = test_CURRENTROOT(self)[2];
    GOBJ_ACT(self)->enemy->corrDirX = test_CURRENTORIENT(self)[0];
    GOBJ_ACT(self)->enemy->corrDirY = test_CURRENTORIENT(self)[1];
    GOBJ_ACT(self)->enemy->corrDirZ = test_CURRENTORIENT(self)[2];
    GOBJ_ACT(self)->enemy->corrDstX = pos[0];
    GOBJ_ACT(self)->enemy->corrDstY = pos[1];
    GOBJ_ACT(self)->enemy->corrDstZ = pos[2];
    if (dir != 0) {
        GOBJ_ACT(self)->enemy->corrDstDirX = dir[0];
        GOBJ_ACT(self)->enemy->corrDstDirY = dir[1];
        GOBJ_ACT(self)->enemy->corrDstDirZ = dir[2];
    }
    GOBJ_ACT(self)->enemy->corrFrames = (int)t;
    GOBJ_ACT(self)->enemy->corrCount = 0;
    ((CorrFlag *)&GOBJ_ACT(self)->enemy->corrMode)->i = mode;
    ((CorrFlag *)&GOBJ_ACT(self)->enemy->corrMode)->ll |= (1ULL << 32);
}

inline int IsCorrectPosition(GObj *self)
{
    unsigned long long v = GOBJ_ACT(self)->enemy->corrFlags;
    return (int)v & 1;
}

/* the orient and the position these two accessors hand back */
static float commonOrient[4]; /* derived name */

static float commonPos[4]; /* derived name */

inline float *test_CURRENTORIENT(GObj *self)
{
    if (self != boyGObj && self != girlGObj && self->kind != 4) {
        GetRootOrient(commonOrient, self);
        return commonOrient;
    }
    {
        float *p = GOBJ_ACT(self)->jump;
        _GetMotionDirection(p, self);
        return p;
    }
}

inline float *test_CURRENTROOT(GObj *self)
{
    float buf[4];
    float *p;
    float v;

    switch (self->kind) {
    case 1:
    case 2:
    case 4:
        p = (float *)(char *)GOBJ_ACT(self);
        /* PC port: the EE spelling is Act + 0x100 through GObj's mail box
           offset (0x54 + 172); on a 64-bit host neither offset holds. */
        p = GOBJ_ACT(self)->curRoot;
        GetRootPosition(p, self);
        return p;
    case 0x2C:
        if (GetCageChainPoint(commonPos, buf, self) == 0) {
            v = 3.40282347e+38f; /* FLT_MAX */
            commonPos[0] = v;
            commonPos[1] = v;
            commonPos[2] = v;
        }
        return commonPos;
    default:
        GetRootPosition(commonPos, self);
        return commonPos;
    }
}

inline void ControlMotionOrient(int id, int mot)
{
    motionOrient[id].w[2] = mot;
}

inline int FloorIsTruck(GObj *self)
{
    GObj *p = GOBJ_SUB(self)->parent.obj;
    if (p != 0) {
        if (p->kind == 17) {
            if (IsThisBoxTruck(p) == 7) {
                return 1;
            }
        }
    }
    return 0;
}

inline void _ACTMotDir_V(void *self, void *dir)
{
    float local[4];
    sceVu0ScaleVector(local, dir, -1.0f);
    SetMotionDirection(self, local);
}

inline void ACTMotDirToWall(GObj *self)
{
    actMotDirToWall(self);
}

inline void SetCorrectOrientOfChain(void *self)
{
    setCorrectOrientOfChain(self);
}

inline void actAfterForceRopeSwing(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    if (s->chain == 0) {
        debug_assert("src/commonact.c", 1653);
        __assert("src/commonact.c", 1653, "ROPE_GOBJ!=NULL");
    }
    UnLockChainGeo(s->chain);
}

static inline void actAfterRopeJump(GObj *volatile self)
{
    char *g = (char *)self;
    GOBJ_ACT(g)->flags20.ll |= (1ULL << 31);
}

static inline void afterCommonRopeCliff(char *self)
{
    char *volatile local = self;
    char *g = ICO_RAW(char *, boyGObj, 0x15C, ((struct GObj *)boyGObj)->dobj);
    ((struct Sub15C *)g)->root.ropeState = 0;
}

static inline void afterCommonRopeTurnSpecial(GObj *volatile self)
{
    char *g = (char *)self;
    GOBJ_SUB(g)->root.ropeState = 0;
}

inline void actAfterDown(GObj *volatile self)
{
    GOBJ_WORK(self)->downTimer = ((60 - systemStatus[0] * 10) / systemStatus[1]) * 0x82 / 0x3C;
}

inline void afterCommonCling(ICO_WORD_PTR(GObj *) volatile self)
{
    ACTGameCollisionOn((volatile int *)self);
}

inline void actAfterSlip(int x)
{
    volatile int local = x;
}

inline void afterCommonRevive(ICO_WORD_PTR(GObj *) volatile self)
{
    ACTGameCollisionOn((volatile int *)self);
}

inline void afterCommonStone(GObj *volatile self)
{
    GObj *g1 = self;
    GObj *g2 = self;
    GOBJ_ACT(g1)->enemy->stonePair = -1;
    GOBJ_ACT(g2)->enemy->word2A4 = 0;
}

inline void afterCommonBox(GObj *volatile self)
{
    _boxbar_set_sound(self, 0);
}

static inline void actAfterJump(GObj *volatile self)
{
    char *g = (char *)self;
    GOBJ_ACT(g)->flags20.ll |= (1ULL << 31);
}

inline void actAfterFall(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    unsigned long long st = s->flags20.ll & ~(1ULL << 42);
    unsigned long long fl = s->flags18.ll & ~(1ULL << 53);
    s->flags18.ll = fl;
    s->flags20.ll = st | (1ULL << 31);
}

typedef struct { /* field names derived */
    char pad0[32];
    unsigned long long status;
} FlySub; /* derived name */

inline void actAfterFly(GObj *volatile self)
{
    Act *s = GOBJ_ACT(self);
    s->flags20.ll |= 0x200;
    SetEnemyFootPrintSwitch(self, 1);
    ResetFlyLimit(self);
}

static inline void ClipCollisionWithField(ClipWork *work)
{
    int tmp[4];
    sceVu0CopyVector(tmp, work->pt[1]);
    ClipWallField(work);
    sceVu0CopyVector(work->pt[1], work->pt[2]);
    ClipFloor(work);
    sceVu0CopyVector(work->pt[1], tmp);
}

inline void afterCommonOneWall(int x)
{
    volatile int local = x;
}

inline int ACTCheckFlagAttack(GObj *self)
{
    return GOBJ_ACT(self)->actMode == 15;
}

typedef struct { /* field names derived */
    char pad0[116];
    int coll;
} BecSub; /* derived name */

static inline void afterCommonBecarry(GObj *volatile self)
{
    SetKidnapInfo(-1, -1);
    GOBJ_SUB(self)->disp = 1;
    ACTGameCollisionOn((volatile int *)self);
    gflagOff(393);
}

inline void afterCommonTruckLever(GObj *volatile self)
{
    char *g = (char *)self;
    SetSwitchState(GOBJ_ACT(g)->env.pullObj, 0);
}
