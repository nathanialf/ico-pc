#include "typedef.h"
#include "sugiCommon.h"
#include "act_a_p_1.h"
#include "debug.h"
#include "pad.h"
#include "obj_manager.h"
#include "act-game.h"
#include "act.h"
#include "boyact.h"
#include "a_p_1.h"
#include "frameDependSequence.h"
#include "geometryManager.h"
#include "particleEffect.h"
#include "quaternion.h"
#include "tableSin.h"
#include "Matrix.h"
#include "matrixDrive.h"
#include "main.h"
#include "motionManager2.h"
#include "spider.h"

typedef struct AP1Vec { /* field names derived */
    float x;
    float y;
    float z;
    float w;
} __attribute__((aligned(16))) AP1Vec; /* derived name */

/* the AI modes are stand, walk, jump, attack, dead and sleep */

/* the two-letter tag GetAP1AIMode hands the debug display. */
static char *ap1ModeTag[] = {"ST", "WA", "JM", "AT", "DE", "SL"}; /* derived name */

inline char *GetAP1AIMode(GObj *self)
{
    Act *p = GOBJ_ACT(self);

    if (p == 0 || (unsigned int)p->actMode >= 6) {
        return "--";
    }
    return ap1ModeTag[p->actMode];
}

/* the fixed hop walkAI asks for when it is boxed in: straight down and
   two units forward. */
static AP1Vec ap1BoxedInJump = {0.0f, -20.0f, 2.0f, 0.0f}; /* derived name */

static int standAI(GObj *self);
static int walkAI(GObj *self);
static int jumpAI(GObj *);
static int attackAI(GObj *);

/* one entry per mode; dead and sleep run no AI of their own. */
static int (*ap1ModeAI[])(GObj *) = {standAI, walkAI, jumpAI, attackAI, 0, 0};

/* the spelled-out mode name hehehe() prints. */
static char *ap1ModeName[] = {"STAND",  "WALK", "JUMP",
                              "ATTACK", "DEAD", "SLEEP"}; /* derived name */

/* the AI's view of the boy and of the object it is watching, refreshed once
   a frame by the sense pass and read by every mode routine */
static float boyDist; /* derived name */ /* distance to the boy */

static short boyPitch; /* derived name */ /* vertical angle to the boy */

static short boyYaw; /* derived name */ /* heading to the boy in local space */

static short boyYawBack; /* derived name */ /* the opposite heading, boyYaw - 32768 */

static int boySafe; /* derived name */ /* the boy is not in danger */

static float lookDist; /* derived name */ /* distance to the object being watched */

static short lookYaw; /* derived name */ /* heading to it in local space */

static AP1Vec boyLocalDir; /* derived name */ /* direction to the boy, in local space */

static AP1Vec boyLocalFlat; /* derived name */ /* the same with y removed and normalised */

static AP1Vec boyDelta; /* derived name */ /* the boy's offset in world space */

static AP1Vec boyDeltaFlat; /* derived name */ /* the same with y removed */

static AP1Vec lookDelta; /* derived name */ /* the watched object's offset in world space */

static AP1Vec lookLocalDir; /* derived name */ /* direction to it, in local space */

static AP1Vec lookLocalFlat; /* derived name */ /* the same with y removed and normalised */

static AP1Vec lookDeltaFlat; /* derived name */ /* lookDelta with y removed */

static AP1Vec selfPos; /* derived name */ /* this actor's own root position */

static int standAI(GObj *self)
{
    Act *p = GOBJ_ACT(self);

    if ((int)(p->flags20.ll >> 21) & 1) {
        short r = boyYaw;
        r = r > 2048 ? 2048 : (r < -2048 ? -2048 : r);
        AP1Turn(self, r);
        return -1;
    }

    if (boyDist < 500.0f) {
        if (p->lookPri != 0) {
            if (AP1MotReq(self, 1))
                return 1;
        }
        if (lookDist > 100.0f) {
            if (AP1MotReq(self, 1))
                return 1;
        }
        if (boyPitch < 16384 && boyDist > 300.0f) {
            short r = boyYaw;
            r = r > 4096 ? 4096 : (r < -4096 ? -4096 : r);
            AP1Turn(self, r);
            return -1;
        }
        if (AP1MotReq(self, 1))
            return 1;
    }

    if (p->lookPri != 0) {
        if (lookDist < 50.0f) {
            short r = lookYaw;
            r = r > 2048 ? 2048 : (r < -2048 ? -2048 : r);
            AP1Turn(self, r);
            return -1;
        }
        if (AP1MotReq(self, 1))
            return 1;
    }

    if (lookDist < 100.0f) {
        short r = boyYaw;
        r = r > 2048 ? 2048 : (r < -2048 ? -2048 : r);
        AP1Turn(self, r);
        return -1;
    }

    return AP1MotReq(self, 1) ? 1 : -1;
}

static int walkAI(GObj *self)
{
    Act *p = GOBJ_ACT(self);

    if ((int)(p->flags20.ll >> 21) & 1) {
        if (AP1MotReq(self, 0))
            return 0;
    }

    if (boyDist < 300.0f) {
        if (p->attack == 0 || boySafe == 0 || boyPitch < 16384) {
            short r;

            if (p->modeFrame >= 31) {
                AP1Turn(self, boyYaw);
                if (AP1JumpReq(self, 2, p->jump)) {
                    p->modeFrame = 0;
                    return 2;
                }
            }
            r = boyYawBack;
            r = r > 12288 ? 12288 : (r < -12288 ? -12288 : r);
            AP1Turn(self, r);
            return -1;
        }
    }

    if (p->attack != 0 && boySafe != 0) {
        if ((boyYaw < 0 ? -boyYaw : boyYaw) < 8192) {
            if (boyDist < 150.0f) {
                short r = boyYaw;

                r = r > 4096 ? 4096 : (r < -4096 ? -4096 : r);
                AP1Turn(self, r);
                if (AP1MotReq(self, 3))
                    return 3;
            }
        }
    }

    if (boyDelta.y > 100.0f) {
        if (VectorLengthSquare(&boyDeltaFlat) < 10000.0f) {
            if (AP1JumpReq(self, 2, &ap1BoxedInJump)) {
                p->modeFrame = 0;
                return 2;
            }
        }
    }

    if (lookDelta.y > 100.0f) {
        if (VectorLengthSquare(&lookDeltaFlat) < 10000.0f) {
            if (AP1JumpReq(self, 2, &ap1BoxedInJump)) {
                p->modeFrame = 0;
                return 2;
            }
        }
    }

    if (p->lookPri != 0) {
        short r = lookYaw;

        r = r > 4096 ? 4096 : (r < -4096 ? -4096 : r);
        AP1Turn(self, r);
        if (lookDist < 50.0f) {
            if (AP1MotReq(self, 0))
                return 0;
        }
        return AP1MotReq(self, 1) == 0 ? -1 : 1;
    }

    if (lookDist < 100.0f) {
        if (AP1MotReq(self, 0))
            return 0;
    }

    if (p->attack != 0 && boySafe != 0 && boyDist < 300.0f) {
        short r = boyYaw;

        r = r > 512 ? 512 : (r < -512 ? -512 : r);
        AP1Turn(self, r);
    }

    {
        short r = lookYaw;

        r = r > 512 ? 512 : (r < -512 ? -512 : r);
        AP1Turn(self, r);
    }
    return AP1MotReq(self, 1) ? 1 : -1;
}

void hehehe(GObj *self)
{
    debug_StdPrintfDummy(ap1ModeName[GOBJ_ACT(self)->actMode]);
}

void SleepAP1(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    s->actMode = 5;
    s->flags18.ll &= ~(1LL << 32);
    GOBJ_ACT(self)->hit = 1;
    AP1MotReqForce(self, 7);
}

void WakeUpAP1(GObj *self)
{
    Act *s = GOBJ_ACT(self);

    if (s->actMode == 4) {
        /* already dead, so it is not woken */
        debug_StdPrintfDummy("既に死んでいるので起こしません\n");
        return;
    }
    s->actMode = 2;
    AP1MotReqForce(self, 2);
    s->flags18.ll |= 1LL << 32;
    {
        Act *t = GOBJ_ACT(self);
        t->attacker = 0;
        t->hit = 0;
    }
}

/* three helpers for subAP1BrainMain */

typedef struct AP1Mtx { /* field names derived */
    float m[16];
} __attribute__((aligned(16))) AP1Mtx; /* derived name */

static inline int AP1GetVerticalAngle(GObj *g, AP1Vec *v) /* derived name */
{
    AP1Vec q;
    AP1Mtx m;
    AP1Vec r;
    int a;

    GetRootQuaternion(&q, g);
    GetInverseQuaternion(&q, &q);
    GetMatrixFromQuaternion(&m, &q);
    _ApplyMatrix(&r, &m, v);
    a = GetTableArcTan2(_Sqrt(r.y * r.y + r.x * r.x), -r.z);
    return a < 0 ? -a : a;
}

static inline float AP1GetDirection(AP1Vec *dst, AP1Vec *tmp, AP1Vec *from,
                                    AP1Vec *to) /* derived name */
{
    float len;

    _SubVectorXYZ(tmp, from, to);
    len = VectorLength(tmp);
    _ScaleVectorXYZ(dst, tmp, 1.0f / len);
    dst->w = 0.0f;
    return len;
}

static inline void AP1ToLocal(GObj *self, AP1Vec *v) /* derived name */
{
    AP1Mtx m;

    GetRootMatrix(&m, self);
    MatrixDrive_SetTransposeMatrix(&m, &m);
    _ApplyMatrix(v, &m, v);
}

/* `self` is volatile: this is an actor sub-thread entry, and _ACTWait
 * yields to the other threads inside the loop */
void subAP1BrainMain(GObj *volatile self)
{
    AP1Vec smooth = {0.0f, 0.0f, 0.0f, 1.0f};
    AP1Vec boy;
    AP1Vec look;
    int hold = 0;
    Act *p;
    GObj *boyObj;
    GObj *host;
    int r;

    p = GOBJ_ACT(self);
    p->actMode = 5;
    p->modeFrame = 0;

    while (1) {
        boyObj = boyGObj;
        GetRootPosition(&selfPos, self);
        GetRootPosition(&boy, boyObj);
        boy.y -= GOBJ_SUB(boyObj)->root.height - 10.0f;
        boyDist = AP1GetDirection(&boyLocalDir, &boyDelta, &boy, &selfPos);
        boyPitch = AP1GetVerticalAngle(boyObj, &boyLocalDir);
        AP1ToLocal(self, &boyLocalDir);
        CopyVector(&boyLocalFlat, &boyLocalDir);
        boyLocalFlat.y = 0.0f;
        _NormalizeVector(&boyLocalFlat, &boyLocalFlat);
        boyYaw = GetTableArcTan2(boyLocalFlat.x, boyLocalFlat.z);
        boyYawBack = boyYaw - 32768;
        CopyVector(&boyDeltaFlat, &boyDelta);
        boyDeltaFlat.y = 0.0f;
        boySafe = IsBoyStatus_NotDanger() == 0;

        host = p->lookTarget;
        if (host != 0) {
            AP1Vec dest;

            GetRootPosition(&dest, host);
            dest.y -= GOBJ_SUB(p->lookTarget)->root.height - 10.0f;
            if (hold == 0) {
                CopyVector(&smooth, &dest);
                hold = 1;
            } else {
                /* sugiCommon.h's multi-block distance_squared_b */
                if (40000.0f < distance_squared_b(&dest, &smooth)) {
                    _InterVectorXYZ(&smooth, &smooth, &dest, 0.5f);
                }
            }
            CopyVector(&look, &smooth);
        } else {
            GetRootPosition(&look, boyObj);
            look.y -= GOBJ_SUB(boyObj)->root.height - 10.0f;
            hold = 0;
        }

        lookDist = AP1GetDirection(&lookLocalDir, &lookDelta, &look, &selfPos);
        AP1ToLocal(self, &lookLocalDir);
        CopyVector(&lookLocalFlat, &lookLocalDir);
        lookLocalFlat.y = 0.0f;
        _NormalizeVector(&lookLocalFlat, &lookLocalFlat);
        lookYaw = GetTableArcTan2(lookLocalFlat.x, lookLocalFlat.z);
        CopyVector(&lookDeltaFlat, &lookDelta);
        lookDeltaFlat.y = 0.0f;

        if (ap1ModeAI[p->actMode] != 0) {
            r = ap1ModeAI[p->actMode](self);
            if (r != -1) {
                p->actMode = r;
            }
        }
        if (p->actMode == 4) {
            break;
        }

        if (CheckFloorAttribute(self, 0x800) || CheckFloorAttribute(self, 0x900)) {
            iosOmSendMail(self, 0xDF, self);
            /* forced death */
            debug_StdPrintfDummy("強制死亡\n");
        }
        p->modeFrame = p->modeFrame + 1;
        _ACTWait(1);
    }

    while (1) {
        _ACTWait(1);
    }
}

static void hitProc(GObj *self)
{
    AP1MotReqForce(self, 5);
}

void SetAP1DeadStatus(GObj *self)
{
    Act *s = GOBJ_ACT(self);
    s->actMode = 4;
    s->flags18.ll &= ~(1LL << 32);
    GOBJ_ACT(self)->hit = 1;
    AP1MotReqForce(self, 5);
}

typedef struct AP1MailEntry { /* field names derived */
    /* 0x0 */ unsigned int mail;
    /* 0x4 */ void *data;
} AP1MailEntry; /* derived name */

/* The GObj's pending-mail box at +0x54 (typedef.h's IosMailBox): a word
   nothing reads, a count and a run of 8-byte slots. */
typedef struct AP1MailQueue { /* field names derived */
    /* 0x00 */ int queue;
    /* 0x04 */ int num;
    /* 0x08 */ AP1MailEntry e[1];
} AP1MailQueue; /* derived name */

/* four helpers for AP1BeforeFunc; SetAP1DeadStatus also uses the first */
static inline void AP1SetMode(GObj *self, int mode) /* derived name */
{
    Act *p = GOBJ_ACT(self);

    p->actMode = mode;
    p->flags18.ll &= ~(1LL << 32);
    GOBJ_ACT(self)->hit = 1;
}

static inline void AP1DeadEffect(GObj *self) /* derived name */
{
    Act *p = GOBJ_ACT(self);

    if (p->actMode != 4) {
        int pos[4];
        int quat[4];

        GetRootPosition(pos, self);
        GetRootQuaternion(quat, self);
        SetParticleEffect(12, pos, quat);
    }
}

static inline void AP1DeadMode(GObj *self) /* derived name */
{
    if (GOBJ_ACT(self)->actMode != 4)
        AP1SetMode(self, 4);
}

static inline void AP1DeadEffectHit(GObj *self) /* derived name */
{
    Act *p = GOBJ_ACT(self);

    if (p->actMode != 4) {
        int pos[4];
        int quat[4];

        AP1SetMode(self, 4);
        GetRootPosition(pos, self);
        GetRootQuaternion(quat, self);
        SetParticleEffect(49, pos, quat);
        hitProc(self);
    }
}

static inline void AP1SetHold(GObj *self) /* derived name */
{
    Act *p = GOBJ_ACT(self);

    p->flags20.ll |= 0x200000;
}

static inline void AP1ClrHold(GObj *self) /* derived name */
{
    Act *p = GOBJ_ACT(self);

    p->flags20.ll &= ~0x200000;
}

void AP1BeforeFunc(GObj *self)
{
    AP1MailQueue *q = ICO_RAWP(AP1MailQueue *, self, 0x54, (AP1MailQueue *)&self->mailBox);
    AP1MailEntry *e = q->e;
    int i;

    for (i = 0; i < q->num; i++, e++) {
        switch (e->mail) {
        default:
            break;

        case 32:
            AP1SetHold(self);
            break;

        case 31:
            AP1ClrHold(self);
            break;

        case 34:
            AP1DeadEffectHit(self);
            break;

        case 13:
            hitProc(self);
            AP1DeadEffect(self);
            AP1DeadMode(self);
            iosPadActRequest(boyPad, 17);
            ExecuteSEPackage(self, 105);
            break;

        case 26:
        case 38:
            AP1DeadEffect(self);
            AP1DeadMode(self);
            hitProc(self);
            break;

        case 223:
            AP1DeadMode(self);
            hitProc(self);
            break;
        }
    }
    q->num = 0;
}

void subAP1Control(int x);

void actAP1Start(GObj *g)
{
    Act *s = actInitialize(g);

    actInitialize_ext_charcter(g);
    s->flags18.ll &= ~(1LL << 32);
    s->actMode = 5;
    s->lookPri = 0;
    s->lookTarget = 0;
    GOBJ_ACT(g)->hit = 1;

    ACTGameView_Add(girlGObj, g);

    _ACTWait(1);

    s->actKind = GetAP1SpecType(g);

    {
        AP1Vec v = {spiderDef[s->actKind].jump[0], spiderDef[s->actKind].jump[1],
                    spiderDef[s->actKind].jump[2], 0};

        CopyVector(s->jump, &v);
    }

    s->attack = (unsigned int)spiderDef[s->actKind].attack;
    actCreateSubThread(subAP1BrainMain, 20);
    actCreateSubThread(subAP1Control, 21);
}

int IsActCharDead(GObj *self)
{
    return ((int)(GOBJ_ACT(self)->flags18.ll >> 32) & 1) ^ 1;
}

void SetAP1HostGObj(GObj *self, GObj *host)
{
    GOBJ_ACT(self)->lookTarget = host;
}

void SetAP1PriorLevel(GObj *self, int val)
{
    GOBJ_ACT(self)->lookPri = val;
}

static inline int jumpAI(GObj *self)
{
    return AP1MotReq(self, 0) ? 0 : -1;
}

static inline int attackAI(GObj *self)
{
    return AP1MotReq(self, 0) ? 0 : -1;
}

inline void subAP1Control(int x)
{
    volatile int local = x;
}
