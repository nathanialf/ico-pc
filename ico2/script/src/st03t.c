#include "st03t.h"
#include "debug.h"
#include "layout_texture.h"
#include "s_init.h"
#include "act.h"
#include "commonact.h"
#include "way_llf.h"
#include "camera-ico2.h"
#include "camera-root.h"
#include "generator.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "box.h"
#include "motionManager2.h"
#include "typedef.h"
#include "main.h"
#include "script.h"

static void actSt03tGirlPosChk(GObj *volatile self);
static void actSt03tGirlUpChk(GObj *volatile self);
static void actSt03tHint1OffChk(GObj *volatile self);

static ActMail switchL_mes[2] = {{430}, {429}}; /* derived name */

static ActMail switchLUp_mes[2] = {{430}, {429}}; /* derived name */

static ActMail switchLChk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail switchLUpChk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail switchR_mes[2] = {{430}, {429}}; /* derived name */

static ActMail switchRUp_mes[2] = {{430}, {429}}; /* derived name */

static ActMail switchRChk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail switchRUpChk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail girlCam_mes[2] = {{430}, {429}}; /* derived name */

static ActMail girlCamStartChk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail girlCamEndChk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail ene_mes[2] = {{430}, {429}}; /* derived name */

static ActMail wayOn_mes[2] = {{430}, {429}}; /* derived name */

static ActMail wayOff_mes[2] = {{430}, {429}}; /* derived name */

static ActMail wayOnChk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail wayOffChk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail girlPos_mes[2] = {{430}, {429}}; /* derived name */

static ActMail girlUp_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hint1Sleep_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hint1OffChk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hint1OnChk_mes[2] = {{430}, {429}}; /* derived name */

void actSt03tSwitchL(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(89) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(364, 360), 0, 0);
        stage_SetAnimation(PAL_JPNKORCN(366, 362), 0, 0);

        switchL_mes[0].func = actSt03tSwitchLChk;
        act->mail = switchL_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(364, 360), 0, 45);
        stage_SetAnimation(PAL_JPNKORCN(366, 362), 0, -1);

        switchLUp_mes[0].func = actSt03tSwitchLUpChk;
        act->mail = switchLUp_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt03tSwitchLChk(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);
    int i;

    i = 0;
    while (i < (60 - systemStatus[0] * 10) / systemStatus[1]) {
        if (scpTriggerFloorAttrTargetMan(self, 0x1000000) != 0) {
            i++;
        } else {
            i = 0;
        }
        _ACTWait(1);
    }

    gflagOn(102);

    stage_SetAnimation(PAL_JPNKORCN(364, 360), 1, 0);
    soundSeDefPlay(1220, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(364, 360), 45, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    gflagOn(89);

    stage_SetAnimation(PAL_JPNKORCN(366, 362), 1, 0);

    SetWayGroupActive(7, 1);

    gflagOn(91);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(366, 362), 75, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    soundSeDefPlay(1218, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(366, 362), 90, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    soundSeDefPlay(1219, 0, 0, 1);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(366, 362)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    switchLChk_mes[0].func = actSt03tSwitchLUpChk;
    act->mail = switchLChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt03tSwitchLUpChk(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    while (scpTriggerFloorAttrTargetMan(self, 0x1000000) != 0) {
        _ACTWait(1);
    }

    _ACTWait(60);

    gflagOff(89);

    stage_SetAnimation(PAL_JPNKORCN(364, 360), 1, 46);
    soundSeDefPlay(1220, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(364, 360), 90, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    stage_SetAnimation(PAL_JPNKORCN(367, 363), 1, 0);

    if (girlGObj != 0) {
        scpCheckDisconnectWallStart(girlGObj);
    }

    SetWayGroupActive(7, 0);

    soundSeDefPlay(1218, 0, 0, 1);
    _ACTWait(30);
    soundSeDefPlay(1219, 0, 0, 1);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(367, 363), 120, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (girlGObj != 0) {
        scpCheckDisconnectWallEnd(girlGObj);
    }

    switchLUpChk_mes[0].func = actSt03tSwitchLChk;
    act->mail = switchLUpChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt03tSwitchR(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(90) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(365, 361), 0, 0);
        stage_SetAnimation(PAL_JPNKORCN(368, 364), 0, 0);

        switchR_mes[0].func = actSt03tSwitchRChk;
        act->mail = switchR_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(365, 361), 0, 45);
        stage_SetAnimation(PAL_JPNKORCN(368, 364), 0, -1);

        switchRUp_mes[0].func = actSt03tSwitchRUpChk;
        act->mail = switchRUp_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt03tSwitchRChk(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);
    int i;

    i = 0;
    while (i < (60 - systemStatus[0] * 10) / systemStatus[1]) {
        if (scpTriggerFloorAttrTargetMan(self, 0x2000000) != 0) {
            i++;
        } else {
            i = 0;
        }
        _ACTWait(1);
    }

    gflagOn(102);

    stage_SetAnimation(PAL_JPNKORCN(365, 361), 1, 0);
    soundSeDefPlay(1220, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(365, 361), 45, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    stage_SetAnimation(PAL_JPNKORCN(368, 364), 1, 0);

    SetWayGroupActive(5, 1);

    gflagOn(91);

    soundSeDefPlay(1218, 0, 0, 1);
    _ACTWait(30);
    soundSeDefPlay(1219, 0, 0, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(368, 364)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    gflagOn(90);

    switchRChk_mes[0].func = actSt03tSwitchRUpChk;
    act->mail = switchRChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt03tSwitchRUpChk(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    while (scpTriggerFloorAttrTargetMan(self, 0x2000000) != 0) {
        _ACTWait(1);
    }

    _ACTWait(((60 - systemStatus[0] * 10) / systemStatus[1]) * 10);

    gflagOff(90);

    stage_SetAnimation(PAL_JPNKORCN(365, 361), 1, 46);
    soundSeDefPlay(1220, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(365, 361), 90, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    stage_SetAnimation(PAL_JPNKORCN(369, 365), 1, 0);

    SetWayGroupActive(5, 0);

    soundSeDefPlay(1218, 0, 0, 1);
    _ACTWait(30);
    soundSeDefPlay(1219, 0, 0, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(369, 365)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    switchRUpChk_mes[0].func = actSt03tSwitchRChk;
    act->mail = switchRUpChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt03tGene(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    Generator_Mask(scpSearchGobj(877));
    Generator_Mask(scpSearchGobj(878));

    while (gflagChk(105) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(20);
    Generator_Call(self);

    Generator_Call(scpSearchGobj(877));
    Generator_Call(scpSearchGobj(878));
}

void actSt03tBoxA(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(298) == 0) {
        scpSearchGobj(865)->active = 0;
    } else {
        scpSearchGobj(852)->active = 0;
        scpSearchGobj(863)->active = 0;
        scpSearchGobj(865)->active = 1;

        scpTransGObj(scpSearchGobj(865), 0.0f, -200.0f, 0.0f);
        _ACTWait(1);
        ReInitBoxGeo(scpSearchGobj(865));
    }
}

void actSt03tBoxB(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(299) == 0) {
        scpSearchGobj(866)->active = 0;
    } else {
        scpSearchGobj(853)->active = 0;
        scpSearchGobj(864)->active = 0;
        scpSearchGobj(866)->active = 1;

        gflagOn(98);

        scpTransGObj(scpSearchGobj(866), 0.0f, -400.0f, 0.0f);
        _ACTWait(1);
        ReInitBoxGeo(scpSearchGobj(866));
    }
}

void actSt03tInit(void)
{
    if (gflagChk(91) != 0) {
        SetWayGroupActive(7, 1);
    } else {
        SetWayGroupActive(7, 0);
    }

    if (gflagChk(92) != 0) {
        SetWayGroupActive(5, 1);
    } else {
        SetWayGroupActive(5, 0);
    }
}

void actSt03tGirlUp(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(93) == 0) {
        girlUp_mes[0].func = actSt03tGirlUpChk;
        act->mail = girlUp_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        FinishHint(12);
    }
}

void actSt03tGirlCam(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    girlCam_mes[0].func = actSt03tGirlCamStartChk;
    act->mail = girlCam_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt03tSekizo(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    scpSekizou(self, 95, PAL_JPNKORCN(80, 76), 0, 18, -913.0f, -400.0f, 605.0f, -1000.0f, -400.0f,
               550.0f);
}

void actSt03tWay(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(100) == 0) {
        wayOn_mes[0].func = actSt03tWayOnChk;
        act->mail = wayOn_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        wayOff_mes[0].func = actSt03tWayOffChk;
        act->mail = wayOff_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt03tEne(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(104) == 0) {
        ene_mes[0].func = actSt03tEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt03tGirlPos(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(101) == 0) {
        SleepHint(12);

        girlPos_mes[0].func = actSt03tGirlPosChk;
        act->mail = girlPos_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt03tHint1Sleep(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    hint1Sleep_mes[0].func = actSt03tHint1OffChk;
    act->mail = hint1Sleep_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt03tGirlCamEvent(int x)
{
    volatile int local = x;
}

void actSt03tGirlCamStartChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerBall(self, boyGObj, 100.0f) == 0 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) != 202) {
        _ACTWait(1);
    }

    CameraGetTarget();
    Camctrl_SetTarget(girlGObj, 0, 3);
    _ACTWait(15);
    CameraSetCameraSet(48);

    girlCamStartChk_mes[0].func = actSt03tGirlCamEndChk;
    sub->mail = girlCamStartChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt03tGirlCamEndChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerBall(self, boyGObj, 100.0f) == 0 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) == 202) {
        _ACTWait(1);
    }

    CameraGetTarget();
    _ACTWait(90);
    CameraSetCameraSet_Default();
    Camctrl_ExitEveRock();

    girlCamEndChk_mes[0].func = actSt03tGirlCamStartChk;
    sub->mail = girlCamEndChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt03tSekizoEvent(int x)
{
    volatile int local = x;
}

void actSt03tEneChk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerBall(self, boyGObj, 100.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyOne(PAL_JPNKORCN(3757, 3734));
    gflagOn(104);
    gflagOn(105);
    stage_SetAnimation(PAL_JPNKORCN(81, 77), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(81, 77)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyOne(PAL_JPNKORCN(3757, 3734));
}

void actSt03tWayOnChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpCheckExistAliveEnemy() != 0 || scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(17, 1);
    gflagOn(100);

    wayOnChk_mes[0].func = actSt03tWayOffChk;
    sub->mail = wayOnChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt03tWayOffChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpCheckExistAliveEnemy() == 0 && scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(17, 0);
    gflagOff(100);

    wayOffChk_mes[0].func = actSt03tWayOnChk;
    sub->mail = wayOffChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt03tGirlPosChk(GObj *volatile self)
{
    while (girlGObj == 0 || scpTriggerFloorAttr(girlGObj, 0x5000000) == 0) {
        _ACTWait(1);
    }

    gflagOn(101);
    WakeupHint(12);
}

static void actSt03tGirlUpChk(GObj *volatile self)
{
    while (girlGObj == 0 || scpTriggerFloorAttr(girlGObj, 0x4000000) == 0) {
        _ACTWait(1);
    }

    debug_StdPrintfDummy("HINT1_FINISH!!!!!!!!!!!!!!!\n");
    gflagOn(93);
    FinishHint(12);
}

static void actSt03tHint1OnChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 60);
    WakeupHint(12);

    hint1OnChk_mes[0].func = actSt03tHint1OffChk;
    sub->mail = hint1OnChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt03tHint1OffChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (gflagChk(102) == 0) {
        _ACTWait(1);
    }

    SleepHint(12);
    gflagOff(102);

    hint1OffChk_mes[0].func = actSt03tHint1OnChk;
    sub->mail = hint1OffChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}
