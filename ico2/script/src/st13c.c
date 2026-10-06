#include "st13c.h"
#include "StageManager.h"
#include "debug.h"
#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
#include "gobj_process.h"
#include "obj_manager.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "act-game.h"
#include "act.h"
#include "commonact.h"
#include "enemy_act.h"
#include "way_llf.h"
#include "camera-ico2.h"
#include "camera-root.h"
#include "fightSound.h"
#include "generator.h"
#include "gflag.h"
#include "script.h"
#include "GsBase.h"
#include "StageAnimation.h"
#include "motionManager2.h"
#include "weapon.h"
#include <libvu0.h>
#include <string.h>
#include "e3.h"
#include "typedef.h"
#include "main.h"
#include "jimaku.h"

static void actSt13cHandSub(GObj *volatile self);

/* the stage-animation number sets the contes play; two functions keep one
   in their frame as the scratch vector they hand scpPlayMotDir or
   DirectCallEnemy. */
typedef struct AnimSet { /* field names derived */
    int anim[5];         /* 0x00 */
} AnimSet;               /* derived name */

typedef struct AnimSet16 { /* field names derived */
    int anim[16];          /* 0x00 */
} AnimSet16;               /* derived name */

/* .sbss: the flag each demo raises when it is over, and the generator the boss
   fight calls through. */
static int demoEnd;

static GObj *bossGenerator; /* derived name */

/* .data: actor mail packets. */

static ActMail bmg1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail sleep_mes[2] = {{430}, {429}}; /* derived name */

static ActMail cageDownMain_mes[2] = {{406, actSt13cCageDownSwitch}, {429}}; /* derived name */

static ActMail cageDown_mes[2] = {{430}, {429}}; /* derived name */

static ActMail cageDownSwitch_mes[2] = {{430}, {429}}; /* derived name */

static ActMail cageFallReady_mes[2] = {{430}, {429}}; /* derived name */

static ActMail cageFall_mes[2] = {{430}, {429}}; /* derived name */

static ActMail cageFall2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail sekizoJimaku_mes[2] = {{430}, {429}}; /* derived name */

static ActMail sekizo_mes[2] = {{430}, {429}}; /* derived name */

static ActMail girlCarry_mes[2] = {{430}, {429}}; /* derived name */

static ActMail girlCarryChk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail girlCarryAgainChk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hand_mes[2] = {{430}, {429}}; /* derived name */

static ActMail rescue_mes[2] = {{430}, {429}}; /* derived name */

static ActMail buki_mes[2] = {{430}, {429}}; /* derived name */

/* .sdata: bmg and hand around the two scene streams, then a word no code
   uses, a shake and its volume. */
SqEntry *bmg = 0;

static SqEntry *st13c_adpcm = 0; /* derived name */

static SqEntry *st13c_adpcm2 = 0; /* derived name */

SqEntry *hand = 0;

static SqEntry *st13c_reserved = 0; /* derived name */

static int st13c_yure = 0; /* derived name */

static unsigned char st13c_yure_vol = 0; /* derived name */

void actSt13cInit(void)
{
    if (gflagChk(21)) {
        SetWayGroupActive(9, 0);
    }
}

void actSt13cEnd(void)
{
    if (gflagChk(31) == 0) {
        debug_StdPrintfDummy("BackStageOff\n");
        gflagOn(390);
    }
}

/* A 16-byte constant vector template: the float view carries the values,
   the long long view is the one the copy reads. */
typedef union { /* field names derived */
    float f[4];
    long long d[2];
} __attribute__((aligned(16))) ConstVecSt13c; /* derived name */

/* The animations actSt13cConte04 steps through. */
static const AnimSet conte04Anims = {{625, 626, 627, 628, 629}}; /* derived name */

/* Where actSt13cSleepChk turns the sleeping girl to face. */
static const ConstVecSt13c sleepFacePos = {{-800.0f, 0.0f, -1000.0f, 1.0f}}; /* derived name */

/* The animations actSt13cConte05 steps through. */
static const AnimSet16 conte05Anims = {
    /* derived name */
    {630, 631, 632, 633, 634, 635, 636, 637, 638, 639, 640, 641, 642, 643, 644, 645}};

/* actSt13cCageFallEffect's nine effect spawns, in the frame order it fires them. */
static const EffectArg cageFallEffect1 = {{-88.0f, -50.0f, -1.0f, 1.0f}}; /* derived name */

static const EffectArg cageFallEffect2 = {{-96.0f, -45.0f, 34.0f, 1.0f}}; /* derived name */

static const EffectArg cageFallEffect3 = {{72.0f, -50.0f, 8.0f, 1.0f}}; /* derived name */

static const EffectArg cageFallEffect4 = {{80.0f, -50.0f, 2.0f, 1.0f}}; /* derived name */

static const EffectArg cageFallEffect5 = {{-27.0f, -50.0f, 100.0f, 1.0f}}; /* derived name */

static const EffectArg cageFallEffect6 = {{-56.0f, -50.0f, 66.0f, 1.0f}}; /* derived name */

static const EffectArg cageFallEffect7 = {{-5.0f, -50.0f, 42.0f, 1.0f}}; /* derived name */

static const EffectArg cageFallEffect8 = {{-10.0f, 0.0f, 466.0f, 1.0f}}; /* derived name */

static const EffectArg cageFallEffect9 = {{-25.0f, 0.0f, 450.0f, 1.0f}}; /* derived name */

void actSt13cBmg1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (girlGObj == 0) {
        return;
    }
    if (gflagChk(21) != 0) {
        return;
    }

    GOBJ_ACT(boyGObj)->flags |= 0x100000;

    if (gflagChk(20) != 0) {
        scpPlayPosSet(girlGObj, -30.0f, -436.0f, -1.0f);
        _ACTWait(60);
        scpPlayStart(girlGObj);
    } else if (gflagChk(18) == 0) {
        ScpCallCameraOff();
        scpPlayPosSet(girlGObj, -7.0f, -5725.0f, 18.0f);

        bmg1_mes[0].func = actSt13cBmg1Chk;
        act->mail = bmg1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        scpPlayPosSet(girlGObj, -7.0f, -5725.0f, 18.0f);
    }
}

void actSt13cBmg1Chk(GObj *volatile self)
{
    GProc *th1;
    GProc *th2;
    unsigned int i;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    ScpCallCameraOn();
    lt_switch_layout(55);
    gflagOn(18);

    scpAdpcmPlayRequestFunc(13, &bmg, 1, 1, 1);

    while (bmg == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    _ACTWait(1);

    th1 = actCreateSubThread(actSt13cConte04, 21);
    th2 = actCreateSubThread(actSt13cConte04Jimaku, 21);

    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    if (demoEnd == 0) {
        scpAdpcmFadeCloseFunc(&bmg, 256);

        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }

        iosThreadSetPri(&((GProc *)th1)->thread, 34);
        iosThreadSetPri(&((GProc *)th2)->thread, 34);

        {
            AnimSet w = conte04Anims;

            for (i = 0; i < 5; i++) {
                stage_SetAnimation(w.anim[i], 1, -1);
                _ACTWait(1);
            }
        }

        jimakuUndisp(&jimaku_msg);

        stage_SetAnimation(629, 1, -1);
        scpFadeIn(3.0f);
    } else {
        iosThreadSetPri(&((GProc *)th1)->thread, 34);
        iosThreadSetPri(&((GProc *)th2)->thread, 34);
    }

    scpPlayMot(boyGObj, 0);

    {
        sceVu0FVECTOR dir;

        sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
        scpPlayMotDir(boyGObj, dir);
    }
    scpPlayEnd(boyGObj);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(54);
}

void actSt13cConte04(GObj *volatile self)
{
    scpPlayStart(boyGObj);

    stage_SetAnimation(625, 1, 0);

    scpPlayMot(boyGObj, 308);
    while (stage_ContinueAnimation(625, 626) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 309);
    while (stage_ContinueAnimation(626, 627) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 310);
    while (stage_ContinueAnimation(627, 628) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 311);
    while (stage_ContinueAnimation(628, 629) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 312);
    while (stage_CheckAnimationFinish(629) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

void actSt13cConte04Jimaku(GObj *volatile self)
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
        case 290:
            jimaku_msg.sub.block = 2;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 800:
            jimaku_msg.sub.block = 4;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 1070:
            jimaku_msg.sub.block = 5;
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
    } while (t < 1400.0f);
    _ACTWait(0);
}

void actSt13cCage1stDownDemoCancel(GObj *volatile self)
{
    float ofs[4];
    float dir[4];
    IOSThread *th;

    demoEnd = 0;

    th = &((GProc *)actCreateSubThread(actSt13cCage1stDownDemo, 21))->thread;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(th, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }
    }

    scpPlayMot(boyGObj, 314);
    scpPlayWaitMotEnd(boyGObj);

    if (demoEnd == 0) {
        scpFadeIn(3.0f);
    }

    while (stage_CheckAnimationFrame(73, 0, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);

    memset(ofs, 0, 16);
    ofs[3] = 1.0f;
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    scpPlayEnd(boyGObj);

    CameraSetCameraSet(37);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(54);
}

void actSt13cCage1stDown(GObj *volatile self)
{
    int se;

    lt_switch_layout(55);
    gflagOn(20);

    scpAdpcmPlayRequestFunc(14, &st13c_adpcm, 1, 1, 1);

    while (st13c_adpcm == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(72, 1, 0);
    stage_SetAnimation(73, 1, 0);

    actCreateSubThread(actSt13cCage1stDownDemoCancel, 21);

    scpPlayStart(girlGObj);

    while (stage_CheckAnimationFrame(72, 15, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMotReq(girlGObj, 286);

    while (stage_CheckAnimationFrame(72, 45, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    se = soundSeDefPlay(1355, 0, 0, 1);

    while (stage_CheckAnimationFinish(72) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    soundSeDefStop(se);

    while (stage_CheckAnimationFinish(72) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

void actSt13cCageFall(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(20) == 0) {
        stage_SetAnimation(72, -1, -2);

        scpSearchGobj(128)->active = 0;
        SetWeaponTorchChainReactionFlagAll(1);

        cageFall_mes[0].func = actSt13cCageFallChk;
        act->mail = cageFall_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        if (gflagChk(22) == 0) {
            scpSearchGobj(128)->active = 0;
            SetWeaponTorchChainReactionFlagAll(1);

            stage_SetAnimation(72, 0, -1);
            stage_SetAnimation(76, 0, 0);

            cageFall2_mes[0].func = actSt13cCageFallChk;
            act->mail = cageFall2_mes;
            ACTSendMailCorrect(self, 430);
            _ACTWait(0);
        }

        scpSearchGobj(144)->active = 0;

        stage_SetAnimation(76, 0, -1);
        stage_SetAnimation(74, 0, -1);
        stage_SetAnimation(75, 0, -1);
    }
}

void actSt13cCageFallChk(GObj *volatile self)
{
    GProc *th1;
    GProc *th2;
    GProc *th3;
    int cancel;
    unsigned int i;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 160.0f) == 0 || gflagChk(20) == 0) {
        _ACTWait(1);
    }

    gflagOn(381);
    CheckPoint();
    gflagOff(381);

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;

    fightSoundProcessRequestPause();
    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    gflagOn(21);

    while (st13c_adpcm2 == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(st13c_adpcm2->stream);

    th1 = actCreateSubThread(actSt13cConte05, 21);
    th2 = actCreateSubThread(actSt13cConte05Jimaku, 21);
    th3 = actCreateSubThread(actSt13cCageFallEffect, 21);

    SetWayGroupActive(9, 0);
    _ACTWait(1);

    stage_SetAnimation(74, 1, 0);
    stage_SetAnimation(75, 1, 0);
    stage_SetAnimation(76, 1, 0);

    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    cancel = demoEnd ^ 1;

    if (cancel) {
        scpAdpcmFadeCloseFunc(&st13c_adpcm2, 256);

        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    }

    iosThreadSetPri(&((GProc *)th1)->thread, 34);
    iosThreadSetPri(&((GProc *)th2)->thread, 34);
    iosThreadSetPri(&((GProc *)th3)->thread, 34);

    if (cancel) {
        {
            AnimSet16 w = conte05Anims;

            for (i = 0; i < 16; i++) {
                stage_SetAnimation(w.anim[i], 1, -1);
                _ACTWait(1);
            }
        }

        jimakuUndisp(&jimaku_msg);

        scpSearchGobj(128)->active = 1;
        scpSearchGobj(129)->active = 1;
        scpSearchGobj(130)->active = 1;
        scpSearchGobj(54)->active = 1;

        scpTorchLightOn(144);
        ResetHandCameraLimitInDemo();
        gflagOn(23);
        _ACTWait(10);

        Generator_QuickCall(bossGenerator);
        Generator_MaskOff(bossGenerator);

        if (isEnemyActive(scpSearchGobj(150)) == 0) {
            sceVu0FVECTOR zero;

            memset(zero, 0, 16);
            DirectCallEnemy(scpSearchGobj(150), bossGenerator, zero, zero, 0);
            iosOmSendMail(scpSearchGobj(150), 258, scpSearchGobj(150));
            _ACTWait(1);
        }

        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayStart(scpSearchGobj(150));

        fightSoundProcessRequestStart();

        stage_SetAnimation(74, 0, -1);
        stage_SetAnimation(75, 0, -1);
        stage_SetAnimation(76, 0, -1);
        stage_SetAnimation(647, 1, 0);

        scpPlayMot(scpSearchGobj(150), 937);
        SetCameraFlag_LwsCutBack();
        scpPlayEnd(scpSearchGobj(150));
        scpWakeupEnemyAll();

        _ACTWait(10);

        scpPlayMot(boyGObj, 323);
        stage_SetAnimation(646, 1, 0);
        scpTorchLightOff(144);
        scpFadeIn(3.0f);
    } else {
        Generator_MaskOff(bossGenerator);
    }

    scpPlayMot(girlGObj, 736);
    scpPlayStart(scpSearchGobj(150));
    scpPlayMot(scpSearchGobj(150), 968);

    while (stage_ContinueAnimation(646, 647) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 324);
    scpPlayMot(scpSearchGobj(150), 937);

    while (stage_CheckAnimationFinish(647) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(scpSearchGobj(150));
    scpWakeupEnemyAll();
    ACTEnemyForceSwitchToCarry(scpSearchGobj(150));
    scpTorchLightOff(144);

    scpSearchGobj(144)->active = 0;

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);

    GOBJ_ACT(scpSearchGobj(150))->flags20.ll |= 0x20000;

    scpBoyControlReadDisable = 0;
    lt_switch_layout(54);

    _ACTWait(30);
    gflagOn(25);
    SetWeaponTorchChainReactionFlagAll(0);

    GOBJ_ACT(boyGObj)->flags &= ~0x100000;
}

void actSt13cConte05(GObj *volatile self)
{
    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    stage_SetAnimation(630, 1, 0);

    scpPlayMot(boyGObj, 315);
    scpPlayMot(girlGObj, 726);

    while (stage_ContinueAnimation(630, 631) == 0) {
        _ACTWait(1);
    }

    SetHandCameraLimitInDemo(0, 0);
    scpPlayMot(girlGObj, 726);

    while (stage_ContinueAnimation(631, 632) == 0) {
        _ACTWait(1);
    }

    ResetHandCameraLimitInDemo();
    scpPlayMot(girlGObj, 726);

    while (stage_ContinueAnimation(632, 633) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(girlGObj, 727);

    while (stage_ContinueAnimation(633, 634) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 316);

    while (stage_ContinueAnimation(634, 635) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 317);

    while (stage_ContinueAnimation(635, 636) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(girlGObj, 728);
    _ACTWait(1);

    scpSearchGobj(128)->active = 1;
    scpSearchGobj(129)->active = 0;
    scpSearchGobj(130)->active = 0;
    scpSearchGobj(54)->active = 0;

    scpTorchLightOn(144);

    while (stage_ContinueAnimation(636, 637) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(girlGObj, 729);

    while (stage_ContinueAnimation(637, 638) == 0) {
        _ACTWait(1);
    }

    SetHandCameraLimitInDemo(5, 5);

    scpSearchGobj(54)->active = 1;

    scpPlayMot(boyGObj, 318);
    scpPlayMot(girlGObj, 730);

    while (stage_ContinueAnimation(638, 639) == 0) {
        _ACTWait(1);
    }

    ResetHandCameraLimitInDemo();

    scpPlayMot(boyGObj, 319);
    scpPlayMot(girlGObj, 731);

    scpSearchGobj(129)->active = 1;
    scpSearchGobj(130)->active = 1;

    _ACTWait(300);
    gflagOn(23);

    while (stage_ContinueAnimation(639, 640) == 0) {
        _ACTWait(1);
    }

    scpPlayStart(scpSearchGobj(150));
    scpPlayMot(scpSearchGobj(150), 964);

    while (stage_ContinueAnimation(640, 641) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 320);
    scpPlayMot(girlGObj, 732);
    scpPlayMot(scpSearchGobj(150), 964);

    while (stage_ContinueAnimation(641, 642) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 321);
    scpPlayMot(girlGObj, 733);
    scpPlayMot(scpSearchGobj(150), 965);

    while (stage_ContinueAnimation(642, 643) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 322);
    scpPlayMot(girlGObj, 734);
    scpPlayMot(scpSearchGobj(150), 966);

    while (stage_ContinueAnimation(643, 644) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(girlGObj, 735);
    scpPlayMot(scpSearchGobj(150), 967);

    while (stage_ContinueAnimation(644, 645) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 323);

    while (stage_ContinueAnimation(645, 646) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(girlGObj, 736);
    scpPlayMot(scpSearchGobj(150), 968);

    fightSoundProcessRequestStart();

    demoEnd = 1;
    _ACTWait(0);
}

void actSt13cConte05Jimaku(GObj *volatile self)
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
        case 1630:
            jimakuOn = 1;
            jimaku_msg.sub.block = 9;
            jimaku_msg.sub.jump = -1;
            jimakuJump(&jimaku_msg);
            break;
        case 2492:
            jimaku_msg.sub.block = 6;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 2760:
            jimaku_msg.sub.block = 7;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 3712:
            jimaku_msg.sub.block = 8;
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
    } while (t < 4000.0f);
    _ACTWait(0);
}

void actSt13cCageFallEffect(GObj *volatile self)
{
    EffectArg b1;
    EffectArg b2;
    EffectArg b3;
    EffectArg b4;
    EffectArg b5;
    EffectArg b6;
    EffectArg b7;
    EffectArg b8;
    EffectArg b9;
    float t;
    float tn;
    int n;

    t = 0.0f;
    do {
        switch ((int)t) {
        case 0:
            gflagOn(22);
            break;
        case 64:
            iosPadActRequest(boyPad, 17);
            b1 = cageFallEffect1;
            scpEffectStart(&b1, 0);
            b2 = cageFallEffect2;
            scpEffectStart(&b2, 0);
            break;
        case 68:
            b3 = cageFallEffect3;
            scpEffectStart(&b3, 0);
            b4 = cageFallEffect4;
            scpEffectStart(&b4, 0);
            break;
        case 96:
            b5 = cageFallEffect5;
            scpEffectStart(&b5, 0);
            b6 = cageFallEffect6;
            scpEffectStart(&b6, 0);
            b7 = cageFallEffect7;
            scpEffectStart(&b7, 0);
            break;
        case 180:
            iosPadActRequest(boyPad, 15);
            break;
        case 300:
            b8 = cageFallEffect8;
            scpEffectStart(&b8, 0);
            b9 = cageFallEffect9;
            scpEffectStart(&b9, 0);
            break;
        case 380:
            iosPadActRequest(boyPad, 16);
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
    } while (t < 400.0f);
    _ACTWait(0);
}

static void actSt13cSekizoChk(GObj *volatile self)
{
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0 || gflagChk(28) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    SetWayGroupActive(2, 1);

    scpAdpcmPlayRequestFunc(17, &hand, 1, 1, 1);
    while (hand == 0) {
        _ACTWait(1);
    }

    scpKillEnemyAll();
    scpMaskGeneratorAll();

    stage_SetAnimation(77, 1, 0);

    st13c_yure = iosPadActRequest(boyPad, 9);
    st13c_yure_vol = 128;
    iosPadActVolumeSet(st13c_yure, 128);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 532);
    scpPlayPosSet(boyGObj, -300.0f, -100.0f, 100.0f);
    scpPlayPosSet(girlGObj, -300.0f, -100.0f, 0.0f);
    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpBoyControlReadDisable = 1;

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpSekizouCheckPoint();

    scpPlayMot(girlGObj, 645);
    scpPlayWaitMotEnd(girlGObj);

    gflagOn(31);

    /* PC port (G2, DIVERGENCES.md F21): the original stops `volatile int
       se`, which nothing writes (st07a's twin stores soundSeDefPlay(1217)
       there first; this one has no play). On the EE it is 4(sp) of the
       96-byte frame (0x24BE14 lw a0,4(sp); the only store near it, 0x24BC14
       sw a0,0(sp), is self). This function is a thread's entry (the mail's
       main, isysGObjProcAdd, iosThreadCreateS over an iosMallocDebug stack
       that nothing clears), entered from iosThreadMain's 32-byte frame
       after only the GetThreadId syscall, so the word is whatever the heap
       block held before: indeterminate. _soundSeDefStop stops a slot only
       when (id >> 8) equals the slot's unsigned short num and its handle is
       live, so any word but a still-live handle of this exact slot is a
       no-op; the host passes -256 (slot 0, id >> 8 = -1, never a num), the
       no-op without -1's read of seSlotTbl[255] past the 48 slots. */
    soundSeDefStop(-256);

    while (stage_CheckAnimationFrame(77, 180, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(st13c_yure);

    while (stage_CheckAnimationFinish(77) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(girlGObj, 532);
    scpPlayEnd(girlGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(54);
}

void actSt13cGirlCarryChk(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (gflagChk(25) == 0 || GOBJ_ACT(girlGObj)->actMode == 111 ||
           GOBJ_ACT(girlGObj)->actMode == 110 || gflagChk(29) != 0) {
        _ACTWait(1);
    }

    _ACTWait(15);

    scpPlayStart(girlGObj);
    scpPlayMot(girlGObj, 595);
    scpPlayWaitMotEnd(girlGObj);
    scpPlayMot(girlGObj, 596);

    GOBJ_SUB(girlGObj)->ctrl.blendFrames =
        (int)((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f * 30.0f);

    gflagOn(26);

    girlCarryChk_mes[0].func = actSt13cGirlCarryAgainChk;
    act->mail = girlCarryChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);

    _ACTWait(1);
}

void actSt13cHandChk(GObj *volatile self)
{
    float dir[4];
    GProc *th1;
    GProc *th2;
    int t;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (1) {
        if (scpActStatusDeathFall(boyGObj) == 0 && actEnemyFlagCheckDead(scpSearchGobj(150)) != 0 &&
            scpTriggerFloorAttr(boyGObj, 0x1000000) == 0 &&
            scpTriggerFloorAttr(boyGObj, 0x3000000) != 0 && gflagChk(26) != 0 &&
            scpTriggerBall(girlGObj, boyGObj, 550.0f) != 0 &&
            (GOBJ_ACT(boyGObj)->pad.trg & 8) != 0 && GOBJ_ACT(girlGObj)->actMode != 110) {
            break;
        }
        if (gflagChk(30) != 0) {
            break;
        }
        _ACTWait(1);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    GOBJ_ACT(scpSearchGobj(150))->flags20.ll &= ~0x20000;

    fightSoundProcessRequestPause();

    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    gsb_SetZoom(2.0f, 1000.0f);
    gflagOn(27);
    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    sceVu0SubVector(dir, test_CURRENTROOT(boyGObj), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpLockMaxRotate(boyGObj, 6.0f);
    _SCPMoveByWay_ToChar(boyGObj, girlGObj, 0, 6, 50.0f, 30.0f);
    scpUnLockMaxRotate(boyGObj);

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    sceVu0SubVector(dir, test_CURRENTROOT(boyGObj), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpAdpcmPlayRequestFunc(16, &st13c_reserved, 1, 1, 1);
    while (st13c_reserved == 0) {
        _ACTWait(1);
    }

    ACTGame_ConnectHand();

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);
    scpPlayMot(boyGObj, 261);
    scpPlayMot(girlGObj, 721);

    th1 = actCreateSubThread(actSt13cHandJimaku, 21);
    th2 = actCreateSubThread(actSt13cHandSub, 21);

    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&((GProc *)th2)->thread, 34);
    iosThreadSetPri(&((GProc *)th1)->thread, 34);

    if (demoEnd == 0) {
        scpAdpcmFadeCloseFunc(&st13c_reserved, 512);

        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        jimakuUndisp(&jimaku_msg);
        scpFadeIn(3.0f);
    }

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 532);

    gsb_SetZoom(1.0f, 1000.0f);
    lt_switch_layout(54);

    t = (60 - systemStatus[0] * 10) / systemStatus[1];
    _ACTWait(t * 3);

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);

    GOBJ_SUB(boyGObj)->ctrl.blendFrames =
        (int)((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f * 30.0f);

    _ACTWait(1);

    iosOmSendMail(girlGObj, 63, boyGObj);

    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
    fightSoundProcessRequestStart();
    gflagOn(28);
}

void actSt13cHandJimaku(GObj *volatile self)
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
        case 110:
            jimaku_msg.sub.block = 11;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 340:
            jimaku_msg.sub.block = 12;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 580:
            jimaku_msg.sub.block = 13;
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
    } while (t < 800.0f);
}

void actSt13cSleep(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(19) == 0) {
        sleep_mes[0].func = actSt13cSleepChk;
        act->mail = sleep_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt13cCageDown(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(20) == 0) {
        stage_SetAnimation(72, 0, 0);
        stage_SetAnimation(76, 0, 0);

        cageDown_mes[0].func = actSt13cCageDownMain;
        act->mail = cageDown_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }

    if (gflagChk(20) != 0) {
        CameraSetCameraSet(37);
    }
}

void actSt13cCageFallReady(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(22) == 0) {
        cageFallReady_mes[0].func = actSt13cCageFallReadyChk;
        act->mail = cageFallReady_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt13cEnemy(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    bossGenerator = self;
    Generator_Mask(self);

    while (gflagChk(23) == 0) {
        _ACTWait(1);
    }

    Generator_Call(scpSearchGobj(152));
    _ACTWait(180);

    Generator_Call(self);
    scpSleepEnemyAll();
    gflagOff(23);
}

void actSt13cEnemyNull(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(25) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);
}

void actSt13cSekizo(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(31) == 0) {
        stage_SetAnimation(77, 0, 0);
        SetWayGroupActive(2, 0);

        sekizo_mes[0].func = actSt13cSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(77, 0, -1);
        SetWayGroupActive(2, 1);
    }
}

void actSt13cSekizoJimaku(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(31) == 0) {
        sekizoJimaku_mes[0].func = actSt13cSekizoJimakuChk;
        act->mail = sekizoJimaku_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt13cHand(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(27) == 0) {
        hand_mes[0].func = actSt13cHandChk;
        act->mail = hand_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt13cGirlCarry(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(27) == 0) {
        girlCarry_mes[0].func = actSt13cGirlCarryChk;
        act->mail = girlCarry_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt13cRescue(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(30) == 0 && gflagChk(27) == 0) {
        rescue_mes[0].func = actSt13cRescueChk;
        act->mail = rescue_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt13cBuki(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    buki_mes[0].func = actSt13cBukiChk;
    act->mail = buki_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actE3St13cSekizo(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    scpSekizou(self, 31, 77, 0, 17, -300.0f, -100.0f, 100.0f, -300.0f, -100.0f, 0.0f);

    if (gflagChk(31) == 0) {
        SetWayGroupActive(2, 0);
    } else {
        SetWayGroupActive(2, 1);
    }
}

void actSt13cBmg1Event(int x)
{
    volatile int local = x;
}

void actSt13cSleepEvent(int x)
{
    volatile int local = x;
}

void actSt13cSleepChk(GObj *volatile self)
{
    long long ofs[2];
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, girlGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    scpPlayStart(girlGObj);
    _ACTWait(1);

    ofs[0] = sleepFacePos.d[0];
    ofs[1] = sleepFacePos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpPlayMotReq(girlGObj, 285);
}

void actSt13cCageDownMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = cageDownMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt13cCageDownSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    scpBoyControlReadDisable = 1;

    if (gflagChk(20) == 0) {
        cageDownSwitch_mes[0].func = actSt13cCage1stDown;
        sub->mail = cageDownSwitch_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt13cCage1stDownDemo(GObj *volatile self)
{
    scpPlayStart(boyGObj);
    scpPlayMot(boyGObj, 313);
    scpPlayWaitMotEnd(boyGObj);
    _ACTWait(240);
    demoEnd = 1;
    _ACTWait(0);
}

void actSt13cCageFallReadyChk(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    while (scpTriggerFloorAttr(boyGObj, 0x2000000) == 0 || gflagChk(20) == 0) {
        _ACTWait(1);
    }

    scpAdpcmPlayRequestFunc(15, &st13c_adpcm2, 1, 1, 0);
}

void actSt13cCageFallEvent(int x)
{
    volatile int local = x;
}

static void actE3St13cSekizoEvent(int x)
{
    volatile int local = x;
}

void actSt13cSekizoJimakuChk(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    while (gflagChk(31) == 0) {
        _ACTWait(1);
    }

    gflagOff(390);
    actCreateSubThread(actSt13cSekizoJimakuEff, 21);
}

void actSt13cSekizoJimakuEff(GObj *volatile self)
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
        case 45:
            jimaku_msg.sub.block = 19;
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
    } while (t < 500.0f);
}

void actSt13cGirlCarryAgainChk(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (GOBJ_ACT(girlGObj)->actMode != 111) {
        _ACTWait(1);
    }

    gflagOff(26);

    girlCarryAgainChk_mes[0].func = actSt13cGirlCarryChk;
    act->mail = girlCarryAgainChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);

    _ACTWait(1);
}

static void actSt13cHandSub(GObj *volatile self)
{
    _ACTWait(100);
    scpPlayWaitMotEnd(boyGObj);
    demoEnd = 1;
    _ACTWait(0);
}

void actSt13cRescueChk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (GOBJ_ACT(girlGObj)->actMode != 110) {
        _ACTWait(1);
    }

    gflagOn(29);
    scpBoyControlReadDisable = 1;
    _ACTWait(200);

    scpPlayStart(girlGObj);
    scpPlayMot(girlGObj, 596);
    _ACTWait(120);

    gflagOn(30);
}

void actSt13cBukiEvent(int x)
{
    volatile int local = x;
}

void actSt13cBukiChk(GObj *volatile self)
{
    while (ForMotionViewer_GetCurrentMotion(boyGObj) != 231) {
        _ACTWait(1);
    }
    stage_SetAnimation(76, -1, -2);
}
