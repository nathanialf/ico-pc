#include "st04a.h"
#include "StageManager.h"
#include "debug.h"
#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
#include "obj_manager.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "act-game.h"
#include "act.h"
#include "gobj_process.h"
#include "boyact.h"
#include "commonact.h"
#include "way_llf.h"
#include "brain.h"
#include "camera-root.h"
#include "fightSound.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "RegistPacket.h"
#include "Shadow.h"
#include "StageAnimation.h"
#include "Texture.h"
#include "geometryManager.h"
#include "girl.h"
#include "matrixDrive.h"
#include "staticBlur.h"
#include "streamMotionManager.h"
#include <libvu0.h>
#include "e3.h"
#include "typedef.h"
#include "Matrix.h"
#include "script.h"
#include "jimaku.h"
#include "main.h"

static void actConte09_3_demoCancel(GObj *volatile self);
static void actSt04aGateLSub(GObj *volatile self);
static void actSt04aGateRSub(GObj *volatile self);
static void actSt04aModelOffChk(GObj *volatile self);
static void actSt04aModelOnChk(GObj *volatile self);
static void finishCallBackFunc(struct GObj *obj);

#ifdef ICO_HOST

/* on the host the shared records are used: Act and GObj are runtime layout (the
   private pad views below are the EE's), and the torch's three words go in
   fields this actor does not otherwise use: torchAnim in Act.pad470, torchObj
   in doorCamera (a door-script field; the EE's 0x474 pointer has no room in
   4 bytes here) and torchFlag in the low word of wish0 (0x478, where the EE
   keeps it) */
typedef Act ActSt04A;

typedef GObj PObjGObjSt04A;

#define TORCH_ANIM(a) (*(int *)(a)->pad470)
#define TORCH_OBJ(a) ((a)->doorCamera)
#define TORCH_FLAG(a) ((a)->wish0.w[0])
#else

/* this file's own view of Act (the shared one is in typedef.h) */
typedef struct ActSt04A { /* field names derived */
    char pad0[32];        /* 0x00 */
    ActStatus flags20;    /* 0x20 */
    char pad28[12];       /* 0x28 */
    int actMode;          /* 0x34, the current action mode */
    char pad38[152];      /* 0x38 */
    ActMail *mainMail;    /* 0xD0 */
    ActMail *mail;        /* 0xD4 */
    char padD8[920];      /* 0xD8 */
    int torchAnim;        /* 0x470, the stage animation the lit torch plays */
    GObj *torchObj;       /* 0x474, the torch the ball must reach */
    int torchFlag;        /* 0x478, the game flag raised once it is lit */
} ActSt04A;               /* derived name */

/* this file's own view of GObj (the shared one is in typedef.h) */
typedef struct PObjGObjSt04A { /* field names derived */
    char pad00[348];           /* 0x000 */
    Sub15C *dobj;              /* 0x15C */
    char pad160[4];            /* 0x160 */
    ActSt04A *act;             /* 0x164 */
    char pad168[4];            /* 0x168 */
    int active;                /* 0x16C */
} PObjGObjSt04A;               /* derived name */

#define TORCH_ANIM(a) ((a)->torchAnim)
#define TORCH_OBJ(a) ((a)->torchObj)
#define TORCH_FLAG(a) ((a)->torchFlag)
#endif

/* .data, ahead of model_on and model_off: each action's mail pair, the check
   handler stored into its first entry at run time, and the matrix
   finishCallBackFunc writes into every joint. */
static ActMail gate_mail[2] = {{430}, {429}}; /* derived name */

static ActMail gate_open_mail[2] = {{430}, {429}}; /* derived name */

static Mtx44 jointMtxInit = {{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f,
                              1.0f, 0.0f, 0.0f, 0.0f, 1.0f}}; /* derived name */

static ActMail gate_open2_ready_mail[2] = {{430}, {429}}; /* derived name */

static ActMail gate_open2_mail[2] = {{430}, {429}}; /* derived name */

static ActMail gate_open3_mail[2] = {{430}, {429}}; /* derived name */

static ActMail gate_l_mail[2] = {{430}, {429}}; /* derived name */

static ActMail gate_r_mail[2] = {{430}, {429}}; /* derived name */

static ActMail torch1_mail[2] = {{430}, {429}}; /* derived name */

static ActMail girl_sit_mail[2] = {{430}, {429}}; /* derived name */

static ActMail torch_hint_mail[2] = {{430}, {429}}; /* derived name */

static ActMail model_mail[2] = {{430}, {429}}; /* derived name */

void actSt04aGate(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    MallocStreamMotionBuffer();

    scpSearchGobj(590)->active = 0;

    if (gflagChk(137) == 0) {
        scpFadeOut(255.0f, 0, 0, 0);

        stage_SetLoopFlag(555, 0);

        stage_SetAnimation(269, 0, 0);
        stage_SetAnimation(272, 0, 0);

        gflagOff(390);

        gate_mail[0].func = actSt04aGateChk;
        act->mail = gate_mail;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetLoopFlag(555, 0);

        stage_SetAnimation(269, 0, -1);
        stage_SetAnimation(272, 0, 0);
    }
}

typedef struct AnimList28 { /* field names derived */
    int v[28];
} AnimList28; /* derived name */

/* stream-motion-def's table; streamMotionManager.h, which defines the
   record, does not declare it */
extern StreamMotionFile streamMotion[];

/* .sdata: the first gate's stream handle and gate1, which no code uses.
   actSt04aConte06's "!!\n" trace follows them. */
SqEntry *gate1st = 0;

int gate1 = 0;

/* .sbss: the demo's own end flag, raised by the subthread the wait loops below
   spin for, and a running flag set for the length of the conte09_3 cutscene
   that nothing reads back. */
static int demoEnd;

static int conte09_3Running; /* derived name */

void actSt04aGateChk(GObj *volatile self)
{
    GProc *th0;
    GProc *th1;
    GProc *th2;
    int i;
    int n;

    stgmgrNextStagePreLoadForceStageSet(0);

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 3000.0f) == 0) {
        _ACTWait(1);
    }

    th0 = actCreateSubThread(actSt04aEnvSe, 21);

    lt_switch_layout(55);

    scpBoyControlReadDisable = 1;

    scpPlayStart(boyGObj);

    scpDispOffAllWithKind(19);

    scpPlayMot(boyGObj, 0);

    StandbyStreamMotion(streamMotion[1].path);

    i = 0;
    while (CheckReadyStreamMotion() == 0) {
        debug_StdPrintfDummy("Now waiting for standby stream motion system... %d\n", ++i);
        _ACTWait(1);
    }

    DisableStreamMotionManagerAutomaticDelete();

    scpPlayStart(girlGObj);

    reg_SetScissorSw(1);

    SetStaticBlur(0);

    scpAdpcmPlayRequestFunc(23, &gate1st, 1, 1, 0);
    while (gate1st == 0) {
        _ACTWait(1);
    }

    gflagOn(137);

    _ACTWait(1);

    th1 = actCreateSubThread(actSt04aConte06, 21);
    th2 = actCreateSubThread(actSt04aConte06Jimaku, 21);

    stage_SetAnimation(269, 1, 0);

    scpSearchGobj(590)->active = 1;

    EntryStreamMotion(boyGObj);
    EntryStreamMotion(girlGObj);
    EntryStreamMotion(scpSearchGobj(590));

    PlayStreamMotion();

    scpFadeIn(6.0f);

    _ACTWait((int)((60 - systemStatus[0] * 10) / systemStatus[1] * 2.5));

    demoEnd = 0;
    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    n = demoEnd ^ 1;

    if (n != 0) {
        scpAdpcmFadeCloseFunc(&gate1st, 192);

        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    }

    DeleteStreamMotionManager();

    iosPadActStopAll();

    iosThreadSetPri(&th1->thread, 34);
    iosThreadSetPri(&th2->thread, 34);
    iosThreadSetPri(&th0->thread, 34);

    if (n != 0) {
        {
            /* the gate-open animations, in the order they start */
            static const AnimList28 gateOpenAnims = {{555, 489, 648, 649, 650, 651, 652, 653,
                                                      654, 655, 656, 657, 658, 659, 660, 661,
                                                      662, 663, 664, 665, 666, 667, 668, 669,
                                                      670, 671, 672, 673}}; /* derived name */
            AnimList28 anim;
            unsigned int j;

            anim = gateOpenAnims;
            for (j = 0; j < 28; j++) {
                stage_SetAnimation(anim.v[j], 1, -1);
                _ACTWait(1);
            }
        }

        jimakuUndisp(&jimaku_msg);

        stage_SetAnimation(269, 0, -1);
        stage_SetAnimation(673, 1, -1);
        stage_SetAnimation(475, -1, -2);
        stage_SetAnimation(477, -1, -2);

        scpSearchGobj(590)->active = 0;

        stage_SetLoopFlag(555, 0);
        stage_SetAnimation(555, -1, -2);

        reg_SetScissorSw(0);

        {
            /* the boy's and the girl's root positions after the gate opens */
            static const ConstVec boyRootPos = {
                {1.635725f, -72.36407f, -1233.2648f, 0.0f}}; /* derived name */
            static const ConstVec girlRootPos = {
                {7.660961f, -88.9936f, -1293.0424f, 0.0f}}; /* derived name */
            long long p1[2];
            long long p2[2];

            p1[0] = boyRootPos.d[0];
            p1[1] = boyRootPos.d[1];
            SetDirectRootPosition(boyGObj, p1);

            p2[0] = girlRootPos.d[0];
            p2[1] = girlRootPos.d[1];
            SetDirectRootPosition(girlGObj, p2);
        }

        _ACTWait(1);

        SetCameraFlag_GamecamCutBack();

        scpFadeIn(3.0f);
    }

    {
        float dir[4];

        scpSeEnvMasterVolRate = 1.0f;

        scpPlayMot(boyGObj, 0);
        scpPlayMot(girlGObj, 532);

        GOBJ_SUB(boyGObj)->ctrl.blendFrames =
            (int)((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f * 0.0f);
        GOBJ_SUB(girlGObj)->ctrl.blendFrames =
            (int)((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f * 0.0f);

        sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
        scpPlayMotDir(boyGObj, dir);

        sceVu0SubVector(dir, test_CURRENTROOT(boyGObj), test_CURRENTROOT(girlGObj));
        scpPlayMotDir(girlGObj, dir);
    }

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);

    _ACTWait(1);

    iosOmSendMail(girlGObj, 63, boyGObj);

    scpBoyControlReadDisable = 0;

    lt_switch_layout(54);

    SetStaticBlur(1);

    gflagOn(155);

    stgmgrNextStagePreLoadDistBoyMode();
}

void actSt04aConte06(GObj *volatile self)
{
    stage_SetAnimation(648, 1, 0);

    AdpcmPlay(gate1st->stream);

    while (stage_ContinueAnimation(648, 649) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(649, 650) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(650, 651) == 0) {
        _ACTWait(1);
    }

    _ACTWait(240);

    gate_yure_low = iosPadActRequest(boyPad, 9);
    gate_yure_low_vol = 64;
    iosPadActVolumeSet(gate_yure_low, 64);

    while (stage_ContinueAnimation(651, 652) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(652, 653) == 0) {
        _ACTWait(1);
    }

    reg_SetScissorSw(0);

    while (stage_ContinueAnimation(653, 654) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(654, 655) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(655, 656) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(475, 1, 0);

    iosPadActRequest(boyPad, 15);

    scpLinkBGAtoKindTargetSkeltonWithLocalRotationFlag(48, 0, 555, 0);

    stage_SetLoopFlag(555, 1);
    stage_SetAnimation(555, 1, 0);

    debug_StdPrintfDummy("!!\n");

    stage_SetAnimation(489, 1, 0);

    tex_SetUVScroll("face_sadow_sd", 0.0f, 0.0f, 0.25f, 0.0625f, 0.99f, 0.99f, 1);
    tex_SetUVScroll("face_sadow_sd_00", 0.0f, 0.0f, 0.25f, 0.0625f, 0.1f, 0.1f, 1);

    while (stage_ContinueAnimation(656, 657) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(657, 658) == 0) {
        _ACTWait(1);
    }

    iosPadActStop(gate_yure_low);

    while (stage_CheckAnimationFrame(658, 150, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    tex_SetUVScroll("face_sadow_sd", 0.0f, 0.0f, 0.25f, 0.0625f, 0.8f, 0.8f, 1);
    tex_SetUVScroll("face_sadow_sd_00", 0.0f, 0.0f, 0.25f, 0.0625f, 0.45f, 0.45f, 1);

    while (stage_ContinueAnimation(658, 659) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(659, 660) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(660, 661) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(661, 662) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(662, 663) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(663, 664) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(664, 665) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(665, 666) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(666, 667) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(667, 668) == 0) {
        _ACTWait(1);
    }

    _ACTWait(120);

    stage_SetAnimation(477, 1, 0);

    iosPadActRequest(boyPad, 15);

    while (stage_ContinueAnimation(668, 669) == 0) {
        _ACTWait(1);
    }

    scpSearchGobj(590)->active = 0;

    stage_SetLoopFlag(555, 0);
    stage_SetAnimation(555, -1, -2);

    while (stage_ContinueAnimation(669, 670) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(670, 671) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(671, 672) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(672, 673) == 0) {
        _ACTWait(1);
    }

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(673) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

/* .sdata, after actSt04aConte06's trace: the gate and torch stream handles,
   then the pad shakes and their volumes (declared in st04a.h for
   actSt04aConte06). */
static SqEntry *gate_open = 0; /* derived name */

static SqEntry *gate_open2 = 0; /* derived name */

static SqEntry *conte09_2 = 0; /* derived name */

SqEntry *gate_ready_l = 0;

SqEntry *gate_ready_r = 0;

SqEntry *torch = 0;

int gate_yure_low = 0;

unsigned char gate_yure_low_vol = 0;

int yure1 = 0;

unsigned char vol1 = 0;

int yure2 = 0;

unsigned char vol2 = 0;

void actSt04aConte06Jimaku(GObj *volatile self)
{
    float t;
    float tn;
    int n;

    t = 0.0f;
    do {
        switch ((int)t) {
        case 1:
            jimakuBegin(&jimaku_msg);
            break;
        case 250:
            jimaku_msg.sub.block = 25;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 1380:
            jimaku_msg.sub.block = 26;
            jimaku_msg.sub.jump = 60;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 2380:
            jimaku_msg.sub.block = 31;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 2861:
            jimaku_msg.sub.block = 34;
            jimaku_msg.sub.jump = 150;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 3160:
            jimaku_msg.sub.block = 35;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 3460:
            jimaku_msg.sub.block = 36;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 3760:
            jimaku_msg.sub.block = 37;
            jimaku_msg.sub.jump = 174;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 4180:
            jimaku_msg.sub.block = 38;
            jimaku_msg.sub.jump = 174;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 4800:
            jimaku_msg.sub.block = 43;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 5580:
            jimaku_msg.sub.block = 27;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 6050:
            jimaku_msg.sub.block = 29;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 6740:
            jimaku_msg.sub.block = 48;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 6980:
            jimaku_msg.sub.block = 49;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        }

        n = (int)t;
        tn = t + (float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f;
        if (n != (int)tn) {
            _ACTWait(1);
            t = tn;
        } else {
            t = tn + 1.0f;
        }
    } while (t < 7300.0f);
    _ACTWait(0);
}

void actSt04aGateOpen(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    MallocStreamMotionBuffer();

    if (gflagChk(140) == 0) {
        stage_SetAnimation(270, 0, 0);

        scpSearchGobj(669)->active = 0;

        gate_open_mail[0].func = actSt04aGateOpenChk;
        act->mail = gate_open_mail;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        SetWayGroupActive(2, 1);

        stage_SetAnimation(270, 0, -1);
        stage_SetAnimation(272, 0, -1);
        stage_SetAnimation(275, 0, -1);

        scpSearchGobj(669)->active = 0;

        SetGirlHairDispSwitch(girlGObj, 1);
    }
}

typedef struct AnimList { /* field names derived */
    int v[13];
} AnimList; /* derived name */

/* .rodata, used here and by actConte09_2: the first lift offset, declared
   ahead and defined with the second after actSt04aGateOpenChk. */
static const ConstVec liftOfs1;

void actSt04aGateOpenChk(GObj *volatile self)
{
    GProc *th1;
    GProc *th2;
    GProc *th3;
    int i;
    int n;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (1) {
        if ((GOBJ_ACT(girlGObj)->actMode != 111 && scpTriggerBall(self, boyGObj, 200.0f) != 0 &&
             scpTriggerBall(self, girlGObj, 200.0f) != 0 && gflagChk(174) != 0 &&
             gflagChk(243) != 0) ||
            (GOBJ_ACT(girlGObj)->actMode != 111 && scpTriggerBall(self, boyGObj, 200.0f) != 0 &&
             scpTriggerBall(self, girlGObj, 200.0f) != 0)) {
            break;
        }
        _ACTWait(1);
    }

    lt_switch_layout(55);

    scpBoyControlReadDisable = 1;

    scpPlayStart(girlGObj);

    scpSleepEnemyAll();

    fightSoundProcessRequestPause();
    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    scpPlayMot(girlGObj, 532);

    StandbyStreamMotion(streamMotion[2].path);

    i = 0;
    while (CheckReadyStreamMotion() == 0) {
        i++;
        debug_StdPrintfDummy("Now waiting for standby stream motion system... %d\n", i);
        _ACTWait(1);
    }

    scpAdpcmPlayRequestFunc(31, &gate_open, 1, 1, 1);
    while (gate_open == 0) {
        _ACTWait(1);
    }

    scpDisActivateAllWithKind(19);

    th1 = actCreateSubThread(actConte09, 21);
    th2 = actCreateSubThread(actSt04aEnvSeWakare1, 21);
    th3 = actCreateSubThread(actConte09Jimaku, 21);

    demoEnd = 0;
    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    n = demoEnd ^ 1;

    if (n != 0) {
        scpAdpcmFadeCloseFunc(&gate_open, 192);

        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    }

    DeleteStreamMotionManager();

    iosPadActStopAll();

    iosThreadSetPri(&th1->thread, 34);
    iosThreadSetPri(&th3->thread, 34);
    iosThreadSetPri(&th2->thread, 34);

    if (n != 0) {
        {
            /* the second gate's animations, in the order they start */
            static const AnimList gateOpen2Anims = {{712, 713, 714, 715, 716, 717, 718, 719, 720,
                                                     721, 722, 723, 724}}; /* derived name */
            AnimList anim;
            unsigned int j;

            anim = gateOpen2Anims;
            for (j = 0; j < 13; j++) {
                stage_SetAnimation(anim.v[j], 1, -1);
                _ACTWait(1);
            }
        }

        jimakuUndisp(&jimaku_msg);

        stage_SetAnimation(724, 1, -1);
        stage_SetAnimation(275, 0, -1);
        stage_SetAnimation(272, 0, -1);
        stage_SetAnimation(280, 0, -1);
        stage_SetAnimation(270, 0, -1);

        scpKillEnemyAll();

        scpDispOnAllWithKind(19);

        gflagOn(140);

        SetGirlHairDispSwitch(girlGObj, 1);

        {
            /* the boy's and the girl's root positions after the second gate */
            static const ConstVec boyRootPos2 = {
                {29.91216f, -71.98232f, -118.11676f, 0.0f}}; /* derived name */
            static const ConstVec girlRootPos2 = {
                {-49.44171f, -76.71414f, -142.27318f, 0.0f}}; /* derived name */
            long long p1[2];
            long long p2[2];

            p1[0] = boyRootPos2.d[0];
            p1[1] = boyRootPos2.d[1];
            SetDirectRootPosition(boyGObj, p1);

            p2[0] = girlRootPos2.d[0];
            p2[1] = girlRootPos2.d[1];
            SetDirectRootPosition(girlGObj, p2);
        }

        _ACTWait(1);

        SetCameraFlag_GamecamCutBack();

        scpFadeIn(3.0f);
    }

    {
        long long ofs[2];
        float dir[4];

        scpSeEnvMasterVolRate = 1.0f;

        scpPlayStart(boyGObj);

        scpPlayMot(boyGObj, 0);

        ofs[0] = liftOfs1.d[0];
        ofs[1] = liftOfs1.d[1];
        sceVu0SubVector(dir, ofs, test_CURRENTROOT(girlGObj));
        scpPlayMotDir(girlGObj, dir);
    }

    _ACTWait(1);

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);

    scpBoyControlReadDisable = 0;

    lt_switch_layout(54);

    SetWayGroupActive(2, 1);

    scpSearchGobj(648)->active = 0;
}

static const ConstVec liftOfs1 = {{0.0f, 0.0f, 6000.0f, 1.0f}}; /* derived name */

static const ConstVec liftOfs2 = {{-5000.0f, 0.0f, 5300.0f, 1.0f}}; /* derived name */

void actConte09(GObj *volatile self)
{
    int th1;
    int th2;

    th1 = EntryStreamMotion(boyGObj);
    th2 = EntryStreamMotion(girlGObj);

    SetStreamMotionFinishCallBackFunc(th1, finishCallBackFunc);
    SetStreamMotionFinishCallBackFunc(th2, finishCallBackFunc);

    PlayStreamMotion();

    scpPlayMot(boyGObj, 0);

    scpSearchGobj(648)->active = 1;

    stage_SetAnimation(712, 1, 0);

    while (stage_ContinueAnimation(712, 713) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(713, 714) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(280, 1, 0);

    while (stage_CheckAnimationFrame(714, 125, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 15);

    scpKillEnemyAll();

    while (stage_ContinueAnimation(714, 715) == 0) {
        _ACTWait(1);
    }
    while (stage_CheckAnimationFrame(715, 15, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    while (stage_ContinueAnimation(715, 716) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(270, 1, 0);

    while (stage_ContinueAnimation(716, 717) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(717, 718) == 0) {
        _ACTWait(1);
    }
    while (stage_CheckAnimationFrame(718, 70, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    SetGirlHairDispSwitch(girlGObj, 1);

    while (stage_ContinueAnimation(718, 719) == 0) {
        _ACTWait(1);
    }

    _ACTWait(300);

    stage_SetAnimation(275, 1, 0);

    _ACTWait(120);

    yure1 = iosPadActRequest(boyPad, 10);
    vol1 = 128;
    iosPadActVolumeSet(yure1, 128);

    while (stage_ContinueAnimation(719, 720) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(272, 1, 0);

    while (stage_ContinueAnimation(720, 721) == 0) {
        _ACTWait(1);
    }

    iosPadActStop(yure1);

    while (stage_ContinueAnimation(721, 722) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(722, 723) == 0) {
        _ACTWait(1);
    }

    scpDispOnAllWithKind(19);

    gflagOn(140);

    while (stage_ContinueAnimation(723, 724) == 0) {
        _ACTWait(1);
    }
    while (stage_CheckAnimationFinish(724) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    GOBJ_SUB(boyGObj)->ctrl.blendFrames =
        (int)((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f * 0.0f);
    GOBJ_SUB(girlGObj)->ctrl.blendFrames =
        (int)((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f * 0.0f);

    demoEnd = 1;

    _ACTWait(0);
}

void actConte09Jimaku(GObj *volatile self)
{
    float t;
    float tn;
    int n;

    t = 0.0f;
    do {
        switch ((int)t) {
        case 1:
            jimakuBegin(&jimaku_msg);
            break;
        case 2161:
            jimaku_msg.sub.block = 88;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 2430:
            jimaku_msg.sub.block = 86;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        }

        n = (int)t;
        tn = t + (float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f;
        if (n != (int)tn) {
            _ACTWait(1);
            t = tn;
        } else {
            t = tn + 1.0f;
        }
    } while (t < 2700.0f);
    _ACTWait(0);
}

void actSt04aGateOpen2Chk(GObj *volatile self)
{
    long long ofs[2];
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(girlGObj, 0x1000000) == 0 || ACTGame_FLAG_TETSUNAGI() == 0) {
        _ACTWait(1);
    }

    GOBJ_ACT(girlGObj)->flags20.ll &= ~0x10000;
    scpPlayMotReq(girlGObj, 315);

    scpPlayMot(boyGObj, 0);

    ofs[0] = liftOfs2.d[0];
    ofs[1] = liftOfs2.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    lt_switch_layout(55);

    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpDisActivateAllWithKind(19);

    while (gate_open2 == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(gate_open2->stream);

    actCreateSubThread(actConte09_2, 21);
}

void actConte09_2(GObj *volatile self)
{
    long long ofs[2];
    float dir[4];
    int i;

    EntryStreamMotion(boyGObj);
    EntryStreamMotion(girlGObj);

    PlayStreamMotion();

    stage_SetAnimation(728, 1, 0);
    stage_SetAnimation(281, 1, 0);

    while (stage_ContinueAnimation(728, 729) == 0) {
        _ACTWait(1);
    }
    while (stage_CheckAnimationFrame(729, 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 15);

    while (stage_ContinueAnimation(729, 730) == 0) {
        _ACTWait(1);
    }
    while (stage_CheckAnimationFrame(730, 15, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    while (stage_ContinueAnimation(730, 731) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(731, 732) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(732, 733) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(733, 734) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(734, 735) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(735, 736) == 0) {
        _ACTWait(1);
    }

    yure2 = iosPadActRequest(boyPad, 10);
    vol2 = 128;
    iosPadActVolumeSet(yure2, 128);

    stage_SetAnimation(273, 1, 0);

    while (stage_ContinueAnimation(736, 737) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(737, 738) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(738, 739) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(739, 740) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(740, 741) == 0) {
        _ACTWait(1);
    }

    ClearStreamMotionEntry(girlGObj);

    scpPlayMotReq(girlGObj, 315);

    scpPlayPosSet(girlGObj, 14.8948f, 210.136f, 4858.48f);

    ofs[0] = liftOfs1.d[0];
    ofs[1] = liftOfs1.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    lt_switch_layout(54);

    while (stage_CheckAnimationFinish(741) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(yure2);

    ofs[0] = liftOfs2.d[0];
    ofs[1] = liftOfs2.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    StandbyStreamMotion(streamMotion[4].path);

    i = 0;
    while (CheckReadyStreamMotion() == 0) {
        i++;
        debug_StdPrintfDummy("Now waiting for standby stream motion system... %d\n", i);
        _ACTWait(1);
    }

    scpAdpcmPlayRequestFunc(33, &conte09_2, 1, 1, 0);

    scpPlayEnd(boyGObj);

    scpBoyControlReadDisable = 0;

    gflagOn(141);
}

void actSt04aGateOpen3(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(142) == 0) {
        stage_SetLoopFlag(555, 0);

        while (stage_CheckAnimationFrame(555, 1, 1) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gate_open3_mail[0].func = actSt04aGateOpen3Chk;
        act->mail = gate_open3_mail;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetLoopFlag(555, 0);

        while (stage_CheckAnimationFrame(555, 1, 1) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
    }
}

void actSt04aGateOpen3Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (gflagChk(141) == 0 || scpTriggerBall(self, boyGObj, 450.0f) == 0) {
        _ACTWait(1);
    }

    gflagOn(142);

    lt_switch_layout(55);

    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    if (gate_open2 != 0) {
        scpAdpcmFadeCloseFunc(&gate_open2, 80);
    }

    while (conte09_2 == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(conte09_2->stream);

    actCreateSubThread(actSt04aEnvSeWakare2, 21);
    actCreateSubThread(actConte09_3, 21);
    actCreateSubThread(actConte09_3Jimaku, 21);
}

void actConte09_3(GObj *volatile self)
{
    conte09_3Running = 1;

    actCreateSubThread(actConte09_3_demoCancel, 21);

    scpSearchGobj(669)->active = 1;

    EntryStreamMotion(boyGObj);
    EntryStreamMotion(girlGObj);
    EntryStreamMotion(scpSearchGobj(669));

    PlayStreamMotion();

    scpSetStreamMotionRootOffset(boyGObj, 0.0f, 0.0f, 1.0f);
    scpSetStreamMotionRootOffset(girlGObj, 0.0f, 0.0f, 1.0f);

    scpTorchLightOff(571);
    scpTorchLightOff(572);
    scpTorchLightOff(573);
    scpTorchLightOff(574);
    scpTorchLightOff(575);
    scpTorchLightOff(576);
    scpTorchLightOff(577);
    scpTorchLightOff(578);
    scpTorchLightOff(579);
    scpTorchLightOff(580);

    stage_SetAnimation(280, -1, -2);
    stage_SetAnimation(281, -1, -2);
    stage_SetAnimation(712, -1, -2);
    stage_SetAnimation(713, -1, -2);
    stage_SetAnimation(714, -1, -2);
    stage_SetAnimation(715, -1, -2);
    stage_SetAnimation(716, -1, -2);
    stage_SetAnimation(717, -1, -2);
    stage_SetAnimation(718, -1, -2);
    stage_SetAnimation(719, -1, -2);
    stage_SetAnimation(720, -1, -2);
    stage_SetAnimation(721, -1, -2);
    stage_SetAnimation(722, -1, -2);
    stage_SetAnimation(723, -1, -2);
    stage_SetAnimation(724, -1, -2);
    stage_SetAnimation(728, -1, -2);
    stage_SetAnimation(729, -1, -2);
    stage_SetAnimation(730, -1, -2);
    stage_SetAnimation(731, -1, -2);
    stage_SetAnimation(732, -1, -2);
    stage_SetAnimation(733, -1, -2);
    stage_SetAnimation(734, -1, -2);
    stage_SetAnimation(735, -1, -2);
    stage_SetAnimation(736, -1, -2);
    stage_SetAnimation(737, -1, -2);
    stage_SetAnimation(738, -1, -2);
    stage_SetAnimation(739, -1, -2);
    stage_SetAnimation(740, -1, -2);
    stage_SetAnimation(741, -1, -2);

    stage_SetAnimation(742, 1, 0);

    _ACTWait(1);

    stage_SetAnimation(273, 1, 502);

    while (stage_ContinueAnimation(742, 743) == 0) {
        _ACTWait(1);
    }

    if (scpGameStat_BoyWeaponkind() == 1) {
        stage_SetAnimation(276, 1, 0);
    }
    if (scpGameStat_BoyWeaponkind() == 4 || scpGameStat_BoyWeaponkind() == 5) {
        stage_SetAnimation(277, 1, 0);
    }
    if (scpGameStat_BoyWeaponkind() == 6) {
        stage_SetAnimation(278, 1, 0);
    }
    if (scpGameStat_BoyWeaponkind() == 9) {
        stage_SetAnimation(279, 1, 0);
    }

    DeleteBoyWeapon();

    while (stage_ContinueAnimation(743, 744) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(744, 745) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(276, -1, -2);
    stage_SetAnimation(277, -1, -2);
    stage_SetAnimation(278, -1, -2);
    stage_SetAnimation(279, -1, -2);

    while (stage_ContinueAnimation(745, 746) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(746, 747) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(747, 748) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(273, 1, 881);

    while (stage_ContinueAnimation(748, 749) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(749, 750) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(725, 1, 0);

    while (stage_ContinueAnimation(750, 751) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(273, 1, 1241);

    shadow_SetLength(GOBJ_SUB(girlGObj), 20.0f);

    while (stage_ContinueAnimation(751, 752) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(273, 1, 2101);
    stage_SetAnimation(725, -1, -2);
    stage_SetAnimation(726, 1, 0);

    while (stage_ContinueAnimation(752, 753) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(273, 1, 2221);

    shadow_DispCancel(71, 1);

    scpLinkBGAtoKindTargetSkeltonWithLocalRotationFlag(48, 0, 555, 0);

    stage_SetLoopFlag(555, 1);
    stage_SetAnimation(555, 1, 0);

    tex_SetUVScroll("face_sadow_sd", 0.0f, 0.0f, 0.25f, 0.0625f, 0.99f, 0.99f, 1);
    tex_SetUVScroll("face_sadow_sd_00", 0.0f, 0.0f, 0.25f, 0.0625f, 0.1f, 0.1f, 1);

    _ACTWait(1);

    scpSearchGobj(649)->active = 0;

    SetStaticBlur(0);

    while (stage_CheckAnimationFrame(753, 150, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    tex_SetUVScroll("face_sadow_sd", 0.0f, 0.0f, 0.25f, 0.0625f, 0.8f, 0.8f, 1);
    tex_SetUVScroll("face_sadow_sd_00", 0.0f, 0.0f, 0.25f, 0.0625f, 0.45f, 0.45f, 1);

    while (stage_ContinueAnimation(753, 754) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    scpSearchGobj(649)->active = 1;

    while (stage_CheckAnimationFrame(754, 50, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    while (stage_ContinueAnimation(754, 755) == 0) {
        _ACTWait(1);
    }

    shadow_SetLength(GOBJ_SUB(girlGObj), 0.0f);

    while (stage_ContinueAnimation(755, 756) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(726, -1, -2);
    stage_SetAnimation(727, 1, 0);

    SetGirlClothDispSwitch(girlGObj, 1, 0);
    SetGirlClothDispSwitch(girlGObj, 0, 0);
    SetGirlClothDispSwitch(girlGObj, 2, 0);

    _ACTWait(1);

    scpSearchGobj(54)->active = 0;
    scpSearchGobj(649)->active = 0;

    shadow_DispCancel(0, 1);
    shadow_DispCancel(4, 1);

    while (stage_ContinueAnimation(756, 757) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(727, -1, -2);

    conte09_3Running = 0;

    _ACTWait(1);

    scpSearchGobj(54)->active = 1;
    scpSearchGobj(649)->active = 1;

    gflagOn(144);
    gflagOn(390);

    stage_SetLoopFlag(555, 0);

    shadow_DispCancel(71, 0);
    shadow_DispCancel(0, 0);
    shadow_DispCancel(4, 0);

    SetStaticBlur(1);

    while (stage_CheckAnimationFrame(757, 90, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpFadeOut(3.0f, 0, 0, 0);

    while (scpFadeChk() != 0) {
        _ACTWait(45);
    }

    RequestStageChange(3, boyGObj, 0, 16.0f, 16.0f);
}

void actSt04aGateLChk(GObj *volatile self)
{
    GProc *th;

    while (gflagChk(174) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);

    scpBoyControlReadDisable = 1;
    gflagOn(138);

    scpAdpcmPlayRequestFunc(29, &gate_ready_l, 1, 1, 1);
    while (gate_ready_l == 0) {
        _ACTWait(1);
    }

    preload(4);

    scpFadeIn(16.0f);

    th = actCreateSubThread(actSt04aGateLSub, 21);

    demoEnd = 0;

    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&th->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        scpAdpcmFadeCloseFunc(&gate_ready_l, 256);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }

        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(293, 0, -1);

        scpFadeIn(3.0f);
    }

    RequestStageChange(4, boyGObj, 0, 1.0f, 8.0f);
}

void actSt04aGateRChk(GObj *volatile self)
{
    GProc *th;
    int stage;

    while (gflagChk(234) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);

    scpBoyControlReadDisable = 1;
    gflagOn(139);

    fightSoundProcessRequestPause();
    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    scpAdpcmPlayRequestFunc(30, &gate_ready_r, 1, 1, 1);
    while (gate_ready_r == 0) {
        _ACTWait(1);
    }

    if (gflagChk(246) != 0) {
        stage = 5;
    } else if (gflagChk(247) != 0) {
        stage = 6;
    } else if (gflagChk(248) != 0) {
        stage = 9;
    } else if (gflagChk(249) != 0) {
        stage = 7;
    } else if (gflagChk(233) != 0) {
        stage = 8;
    } else {
        stage = 0;
    }

    preload(stage);

    scpFadeIn(16.0f);

    th = actCreateSubThread(actSt04aGateRSub, 21);

    demoEnd = 0;

    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&th->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        scpAdpcmFadeCloseFunc(&gate_ready_r, 256);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }

        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(295, 0, -1);

        scpFadeIn(3.0f);
    }

    RequestStageChange(stage, boyGObj, 0, 1.0f, 8.0f);
}

void actSt04aTorch1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(153) == 0) {
        SleepHint(6);
        SleepHint(4);

        if (gflagChk(145) != 0) {
            scpSearchGobj(563)->active = 0;
            stage_SetAnimation(283, 0, -1);
        }

        if (gflagChk(146) != 0) {
            scpSearchGobj(564)->active = 0;
            stage_SetAnimation(284, 0, -1);
        }

        if (gflagChk(147) != 0) {
            scpSearchGobj(565)->active = 0;
            stage_SetAnimation(285, 0, -1);
        }

        if (gflagChk(148) != 0) {
            scpSearchGobj(566)->active = 0;
            stage_SetAnimation(286, 0, -1);
        }

        if (gflagChk(149) != 0) {
            scpSearchGobj(567)->active = 0;
            stage_SetAnimation(287, 0, -1);
        }

        if (gflagChk(150) != 0) {
            scpSearchGobj(568)->active = 0;
            stage_SetAnimation(288, 0, -1);
        }

        if (gflagChk(151) != 0) {
            scpSearchGobj(569)->active = 0;
            stage_SetAnimation(289, 0, -1);
        }

        if (gflagChk(152) != 0) {
            scpSearchGobj(570)->active = 0;
            stage_SetAnimation(290, 0, -1);
        }

        torch1_mail[0].func = actSt04aTorch1Chk;
        act->mail = torch1_mail;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        scpSearchGobj(563)->active = 0;
        scpSearchGobj(564)->active = 0;
        scpSearchGobj(565)->active = 0;
        scpSearchGobj(566)->active = 0;
        scpSearchGobj(567)->active = 0;
        scpSearchGobj(568)->active = 0;
        scpSearchGobj(569)->active = 0;
        scpSearchGobj(570)->active = 0;

        stage_SetAnimation(283, 0, -1);
        stage_SetAnimation(284, 0, -1);
        stage_SetAnimation(285, 0, -1);
        stage_SetAnimation(286, 0, -1);
        stage_SetAnimation(287, 0, -1);
        stage_SetAnimation(288, 0, -1);
        stage_SetAnimation(289, 0, -1);
        stage_SetAnimation(290, 0, -1);

        scpTorchLightOn(571);
        scpTorchLightOn(572);
        scpTorchLightOn(573);
        scpTorchLightOn(574);
        scpTorchLightOn(575);
        scpTorchLightOn(576);
        scpTorchLightOn(577);
        scpTorchLightOn(578);
        scpTorchLightOn(579);
        scpTorchLightOn(580);
    }
}

void actSt04aTorch1Chk(GObj *volatile self)
{
    GObj *x = self;
    ActSt04A *act = actInitialize(self);
    _ACTWait(1);

    switch (self->labelId) {
    case 555:
        TORCH_FLAG(act) = 145;
        TORCH_OBJ(act) = scpSearchGobj(563);
        TORCH_ANIM(act) = 283;
        actCreateSubThread(actSt04aTorchAllFlagfChk, 21);
        break;
    case 556:
        TORCH_FLAG(act) = 146;
        TORCH_OBJ(act) = scpSearchGobj(564);
        TORCH_ANIM(act) = 284;
        break;
    case 557:
        TORCH_FLAG(act) = 147;
        TORCH_OBJ(act) = scpSearchGobj(565);
        TORCH_ANIM(act) = 285;
        break;
    case 558:
        TORCH_FLAG(act) = 148;
        TORCH_OBJ(act) = scpSearchGobj(566);
        TORCH_ANIM(act) = 286;
        break;
    case 559:
        TORCH_FLAG(act) = 149;
        TORCH_OBJ(act) = scpSearchGobj(567);
        TORCH_ANIM(act) = 287;
        break;
    case 560:
        TORCH_FLAG(act) = 150;
        TORCH_OBJ(act) = scpSearchGobj(568);
        TORCH_ANIM(act) = 288;
        break;
    case 561:
        TORCH_FLAG(act) = 151;
        TORCH_OBJ(act) = scpSearchGobj(569);
        TORCH_ANIM(act) = 289;
        break;
    case 562:
        TORCH_FLAG(act) = 152;
        TORCH_OBJ(act) = scpSearchGobj(570);
        TORCH_ANIM(act) = 290;
        break;
    }

    while (1) {
        if (scpTriggerBall(self, TORCH_OBJ(act), 5.0f) != 0) {
            scpBoyControlReadDisable = 1;

            TORCH_OBJ(act)->active = 0;

            stage_SetAnimation(TORCH_ANIM(act), 1, 0);

            while (stage_CheckAnimationFrame(TORCH_ANIM(act), 2, 0) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);

            soundSeDefPlay(1363, 0, 0, 1);

            while (stage_CheckAnimationFinish(TORCH_ANIM(act)) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);

            gflagOn(TORCH_FLAG(act));

            scpBoyControlReadDisable = 0;
            break;
        }
        _ACTWait(1);
    }
}

void actSt04aTorchAllFlagfChk(GObj *volatile self)
{
    int i;
    int skip = 0;

    while (gflagChk(145) == 0 || gflagChk(146) == 0 || gflagChk(147) == 0 || gflagChk(148) == 0 ||
           gflagChk(149) == 0 || gflagChk(150) == 0 || gflagChk(151) == 0 || gflagChk(152) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();
    gflagOn(153);
    WakeupHint(6);
    WakeupHint(4);
    scpAdpcmPlayRequestFunc(85, &torch, 1, 1, 1);
    while (torch == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(282, 1, 0);
    while (stage_CheckAnimationFrame(282, 60, 0) == 0 && skip == 0) {
        if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
            skip = 1;
        }
        _ACTWait(1);
    }

    scpTorchLightOn(571);
    scpTorchLightOn(572);
    scpTorchLightOn(573);
    scpTorchLightOn(574);
    scpTorchLightOn(575);
    scpTorchLightOn(576);
    scpTorchLightOn(577);
    scpTorchLightOn(578);
    scpTorchLightOn(579);
    scpTorchLightOn(580);

    while (stage_CheckAnimationFinish(282) == 0 && skip == 0) {
        if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
            skip = 1;
        }
        _ACTWait(1);
    }

    for (i = 60; i-- > 0 && skip == 0;) {
        if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
            skip = 1;
        }
        _ACTWait(1);
    }

    if (skip != 0) {
        scpAdpcmFadeCloseFunc(&torch, 192);
        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        stage_SetAnimation(282, 0, -1);
        SetCameraFlag_LwsCutBack();
        scpFadeIn(3.0f);
    }
    scpBoyControlReadDisable = 0;
    lt_switch_layout(54);
    scpWakeupEnemyAll();
}

static void actSt04aTorchHintChk(GObj *volatile self)
{
    while (gflagChk(155) == 0) {
        _ACTWait(1);
    }

    OverrideBrainStatusByGObj(&brainGirl, scpSearchGobj(563), 1.0f, 0.001f, 1.0f);
    OverrideBrainStatusByGObj(&brainGirl, scpSearchGobj(564), 1.0f, 0.001f, 1.0f);
    OverrideBrainStatusByGObj(&brainGirl, scpSearchGobj(565), 1.0f, 0.001f, 1.0f);
    OverrideBrainStatusByGObj(&brainGirl, scpSearchGobj(566), 1.0f, 0.001f, 1.0f);
    OverrideBrainStatusByGObj(&brainGirl, scpSearchGobj(567), 1.0f, 0.001f, 1.0f);
    OverrideBrainStatusByGObj(&brainGirl, scpSearchGobj(568), 1.0f, 0.001f, 1.0f);
    OverrideBrainStatusByGObj(&brainGirl, scpSearchGobj(569), 1.0f, 0.001f, 1.0f);
    OverrideBrainStatusByGObj(&brainGirl, scpSearchGobj(570), 1.0f, 0.001f, 1.0f);
    OverrideBrainStatusByGObj(&brainGirl, scpSearchGobj(591), 1.0f, 0.001f, 1.0f);

    if (gflagChk(156) == 0) {
        _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 20);
    }

    OverrideBrainStatusByGObj(&brainGirl, scpSearchGobj(563), 10.0f, 0.01f, 1.0f);
    OverrideBrainStatusByGObj(&brainGirl, scpSearchGobj(564), 10.0f, 0.01f, 1.0f);
    OverrideBrainStatusByGObj(&brainGirl, scpSearchGobj(565), 10.0f, 0.01f, 1.0f);
    OverrideBrainStatusByGObj(&brainGirl, scpSearchGobj(566), 10.0f, 0.01f, 1.0f);
    OverrideBrainStatusByGObj(&brainGirl, scpSearchGobj(567), 10.0f, 0.01f, 1.0f);
    OverrideBrainStatusByGObj(&brainGirl, scpSearchGobj(568), 10.0f, 0.01f, 1.0f);
    OverrideBrainStatusByGObj(&brainGirl, scpSearchGobj(569), 10.0f, 0.01f, 1.0f);
    OverrideBrainStatusByGObj(&brainGirl, scpSearchGobj(570), 10.0f, 0.01f, 1.0f);
    OverrideBrainStatusByGObj(&brainGirl, scpSearchGobj(591), 2.0f, 0.01f, 1.0f);

    gflagOn(156);
}

void actSt04aGateL(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(138) == 0) {
        gflagOn(389);

        scpFadeOut(255.0f, 0, 0, 0);

        stage_SetAnimation(295, 0, 0);

        gate_l_mail[0].func = actSt04aGateLChk;
        act->mail = gate_l_mail;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04aGateR(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(139) == 0) {
        gflagOn(389);

        scpFadeOut(255.0f, 0, 0, 0);

        stage_SetAnimation(293, 0, -1);

        gate_r_mail[0].func = actSt04aGateRChk;
        act->mail = gate_r_mail;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04aTorchXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    stage_SetAnimation(291, 0, 0);
}

void actSt04aDeadCam(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(141) == 0 || scpTriggerBall(self, boyGObj, 600.0f) == 0) {
        _ACTWait(1);
    }

    while (stage_CheckAnimationFrame(273, 1400, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    stage_SetAnimation(274, 1, 0);

    while (stage_CheckAnimationFinish(274) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

void actSt04aGateOpen2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(141) == 0) {
        scpSearchGobj(669)->active = 0;

        gate_open2_mail[0].func = actSt04aGateOpen2Chk;
        act->mail = gate_open2_mail;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04aGateOpen2Ready(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    gate_open2_ready_mail[0].func = actSt04aGateOpen2ReadyChk;
    act->mail = gate_open2_ready_mail;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04aGirlSit(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    girl_sit_mail[0].func = actSt04aGirlSitChk;
    act->mail = girl_sit_mail;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04aTorchHint(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(153) == 0) {
        torch_hint_mail[0].func = actSt04aTorchHintChk;
        act->mail = torch_hint_mail;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04aModel(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    scpSearchGobj(648)->active = 0;

    model_mail[0].func = actSt04aModelOnChk;
    act->mail = model_mail;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04aEnvSe(GObj *volatile self)
{
    float f = 0.0f;

    scpSeEnvMasterVolRate = 0.0f;

    for (;;) {
        float nf = f + (float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f;
        if ((int)f != (int)nf) {
            _ACTWait(1);
            f = nf;
        } else {
            f = nf + 1.0f;
        }
        if ((int)f >= 5401) {
            scpSeEnvMasterVolRate += 1.0f / 1800.0f;
            if (scpSeEnvMasterVolRate > 1.0f) {
                scpSeEnvMasterVolRate = 1.0f;
                break;
            }
        }
    }
    _ACTWait(0);
}

void actSt04aEnvSeWakare1(GObj *volatile self)
{
    float f = 0.0f;

    scpSeEnvMasterVolRate = 0.0f;

    for (;;) {
        float nf = f + (float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f;
        if ((int)f != (int)nf) {
            _ACTWait(1);
            f = nf;
        } else {
            f = nf + 1.0f;
        }
        if ((int)f >= 2201) {
            scpSeEnvMasterVolRate += 1.0f / 3800.0f;
            if (scpSeEnvMasterVolRate > 1.0f) {
                scpSeEnvMasterVolRate = 1.0f;
                break;
            }
        }
    }
    _ACTWait(0);
}

typedef struct { /* field names derived */
    float m[4];
} Vec4St04A; /* derived name */

static void finishCallBackFunc(GObj *obj)
{
    Vec4St04A v;
    int i;

    _ApplyMatrix(&v, (void *)GOBJ_SUB(obj)->nodeMtx, YUnitVector);
    v.m[1] = 0.0f;
    _NormalizeVector(GOBJ_SUB(obj)->ctrl.dir, &v);

    for (i = 0; i < GOBJ_SUB(obj)->skelNodeNum; i++) {
        *(Mtx44 *)&GOBJ_SUB(obj)->nodeRotElem[i] = jointMtxInit;
    }
}

void actSt04aGateOpen2ReadyChk(GObj *volatile self)
{
    GObj *x = self;
    int i;

    actInitialize(self);
    _ACTWait(1);

    while (girlGObj == 0 || scpTriggerFloorAttr(girlGObj, 0x2000000) == 0) {
        _ACTWait(1);
    }

    StandbyStreamMotion(streamMotion[3].path);

    i = 0;
    while (CheckReadyStreamMotion() == 0) {
        i++;
        debug_StdPrintfDummy("Now waiting for standby stream motion system... %d\n", i);
        _ACTWait(1);
    }

    scpAdpcmPlayRequestFunc(32, &gate_open2, 1, 0, 0);
}

void actSt04aEnvSeWakare2(GObj *volatile self)
{
    float f = 0.0f;

    for (;;) {
        float nf = f + (float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f;
        if ((int)f != (int)nf) {
            _ACTWait(1);
            f = nf;
        } else {
            f = nf + 1.0f;
        }
        if ((int)f >= 2351) {
            scpSeEnvMasterVolRate -= 1.0f / 720.0f;
            if (scpSeEnvMasterVolRate < 0.0f) {
                scpSeEnvMasterVolRate = 0.0f;
                break;
            }
        }
    }
    _ACTWait(0);
}

void actConte09_3Jimaku(GObj *volatile self)
{
    float t;
    float tn;
    int n;

    t = 0.0f;
    do {
        switch ((int)t) {
        case 1:
            jimakuBegin(&jimaku_msg);
            break;
        case 2470:
            jimaku_msg.sub.block = 91;
            jimaku_msg.sub.jump = 300;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        }

        n = (int)t;
        tn = t + (float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f;
        if (n != (int)tn) {
            _ACTWait(1);
            t = tn;
        } else {
            t = tn + 1.0f;
        }
    } while (t < 3000.0f);
}

static void actConte09_3_demoCancel(GObj *volatile self)
{
    while (1) {
        _ACTWait(1);
    }
}

static void actSt04aGateLSub(GObj *volatile self)
{
    stage_SetAnimation(293, 1, 0);
    while (stage_CheckAnimationFinish(293) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

static void actSt04aGateRSub(GObj *volatile self)
{
    stage_SetAnimation(295, 1, 0);
    while (stage_CheckAnimationFinish(295) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

void actSt04aGirlSitChk(GObj *volatile self)
{
    int n;

    while (gflagChk(140) == 0) {
        _ACTWait(1);
    }
    GOBJ_ACT(girlGObj)->flags20.ll |= 0x10000;
    n = 0;
    for (;;) {
        if ((int)(GOBJ_ACT(girlGObj)->flags20.ll >> 20) & 1) {
            n++;
        } else {
            n = 0;
        }
        if (((60 - systemStatus[0] * 10) / systemStatus[1]) * 3 < n) {
            iosOmSendMail(girlGObj, 109, girlGObj);
            n = 0;
        }
        _ACTWait(1);
    }
}

/* The model-on watcher's mail record: it installs actSt04aModelOffChk here
   and posts it. Word 0 of each entry is the mail id the entry answers (430
   the actor post, 429 the trailing entry); .func is filled in at run time.
   Named for the thread that owns and posts it. */
static ActMail model_on[2] = {{430}, {429}}; /* derived name */

static void actSt04aModelOnChk(GObj *volatile self)
{
    ActSt04A *sub = ((PObjGObjSt04A *)self)->act;

    while (scpTriggerFloorAttr(boyGObj, 0x3000000) != 0) {
        _ACTWait(1);
    }

    scpSearchGobj(648)->active = 1;

    model_on[0].func = actSt04aModelOffChk;
    sub->mail = model_on;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

/* The model-off watcher's own mail record (installs actSt04aModelOnChk). */
static ActMail model_off[2] = {{430}, {429}}; /* derived name */

static void actSt04aModelOffChk(GObj *volatile self)
{
    ActSt04A *sub = ((PObjGObjSt04A *)self)->act;

    while (scpTriggerFloorAttr(boyGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }

    scpSearchGobj(648)->active = 0;

    model_off[0].func = actSt04aModelOnChk;
    sub->mail = model_off;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}
