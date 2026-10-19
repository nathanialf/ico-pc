#include "st04b.h"
#include "layout_texture.h"
#include "pad.h"
#include "s_init.h"
#include "act.h"
#include "commonact.h"
#include "brain.h"
#include "camera-root.h"
#include "generator.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "item.h"
#include <libvu0.h>
#include "typedef.h"
#include "main.h"
#include "script.h"

static ActMail sekizo_mes[2] = {{430}, {429}}; /* derived name */

static ActMail ene1_mes[2] = {{430}, {429}}; /* derived name */

/* A 16-byte constant vector template: the float view carries the values,
   the long long view is the one the copy reads. */

static const ConstVec girlWayPos = {{-10750.0f, -2122.0f, 0.0f, 0.0f}}; /* derived name */

static const ConstVec girlWay2Pos = {{-139.0f, -177.0f, 1670.0f, 0.0f}}; /* derived name */

void actSt04bEnd(void)
{
    if (girlGObj != 0) {
        if (gflagChk(138) != 0) {
            if (gflagChk(157) == 0) {
                gflagOn(391);
            }
        }
    }
}

/* .sdata: the stone statue's stream handle, its shake and the shake's volume. */
SqEntry *sekizo4b = 0;

int sekizo_4b = 0;

unsigned char sekizo_4b_vol = 0;

void actSt04bSekizoChk(GObj *volatile self)
{
    /* soundSeDefPlay hands back a slot id the sound side keeps updating, so
       the handle is read again at the stop site */
    volatile int se;
    float v[4];
    int key;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKOR(55, 52));
    scpBoyControlReadDisable = 1;
    brainLockGirl();

    scpKillEnemyOne(PAL_JPNKOR(3757, 3734));
    scpMaskGeneratorAll();

    scpAdpcmPlayRequestFunc(18, &sekizo4b, 1, 1, 1);
    while (sekizo4b == 0) {
        _ACTWait(1);
    }

    gflagOn(390);

    stage_SetAnimation(PAL_JPNKOR(191, 187), 1, 0);

    ReviveAllCarryableItemsWithNonSleepFrame(250);

    key = iosPadActRequest(boyPad, 9);
    sekizo_4b_vol = 128;
    iosPadActVolumeSet(sekizo_4b = key, 128);

    se = soundSeDefPlay(1217, 0, 0, 1);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 532);

    scpPlayPosSet(girlGObj, -10325.0f, -2150.0f, 0.0f);
    scpPlayPosSet(boyGObj, -10325.0f, -2150.0f, -100.0f);
    _ACTWait(1);
    sceVu0SubVector(v, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, v);
    scpBoyControlReadDisable = 1;
    sceVu0SubVector(v, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, v);

    scpSekizouCheckPoint();

    scpPlayMot(girlGObj, 645);
    scpPlayWaitMotEnd(girlGObj);

    gflagOn(159);

    soundSeDefStop(se);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(191, 187), 151, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(sekizo_4b);

    scpPlayMot(girlGObj, 532);
    scpPlayEnd(girlGObj);

    actCreateSubThread(actSt04bGirlWay, 21);

    _ACTWait(30);
    scpPlayMot(boyGObj, 252);
    scpPlayWaitMotEnd(boyGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    ScpCallCameraSetTarget(10793.0f, 2122.0f, 0.0f);

    while (stage_CheckAnimationFinish(PAL_JPNKOR(191, 187)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(PAL_JPNKOR(54, 51));
    scpBoyControlReadDisable = 0;
}

void actSt04bEne1Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (gflagChk(138) == 0 || (scpTriggerFloorAttr(girlGObj, 0x1000000) == 0 &&
                                  scpTriggerFloorAttr(boyGObj, 0x2000000) == 0)) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKOR(55, 52));
    scpBoyControlReadDisable = 1;

    scpSleepEnemyOne(PAL_JPNKOR(3757, 3734));

    gflagOff(391);

    _ACTWait(60);

    gflagOn(157);
    gflagOn(158);

    stage_SetAnimation(PAL_JPNKOR(184, 180), 1, 0);
    SetCameraFlag_LwsCutBack();
    while (stage_CheckAnimationFinish(PAL_JPNKOR(184, 180)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(PAL_JPNKOR(54, 51));
    scpBoyControlReadDisable = 0;

    scpWakeupEnemyOne(PAL_JPNKOR(3757, 3734));
}

void actSt04bCrest01XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(174) == 0) {
        stage_SetAnimation(PAL_JPNKOR(185, 181), 0, 0);
        stage_SetAnimation(PAL_JPNKOR(186, 182), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(185, 181), 0, -1);
        stage_SetAnimation(PAL_JPNKOR(186, 182), 0, -1);
        scpTorchLightOn(1056);
        scpTorchLightOn(1057);
    }
}

void actSt04bDoorXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    stage_SetAnimation(PAL_JPNKOR(249, 245), 0, 0);
}

void actSt04bMonyoDoorXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(174) == 0) {
        stage_SetAnimation(PAL_JPNKOR(250, 246), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(250, 246), 0, -1);
    }
}

void actSt04bSekizo(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(159) == 0) {
        stage_SetAnimation(PAL_JPNKOR(191, 187), 0, 0);
        sekizo_mes[0].func = actSt04bSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(191, 187), 0, -1);
        if (gflagChk(174) == 0) {
            ScpCallCameraSetTarget(10793.0f, 2122.0f, 0.0f);
        }
    }
}

void actSt04bEne1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(157) == 0) {
        ene1_mes[0].func = actSt04bEne1Chk;
        act->mail = ene1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04bEnemy1(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);
    while (gflagChk(158) == 0) {
        _ACTWait(1);
    }
    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt04bEnemy2(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);
    while (gflagChk(158) == 0) {
        _ACTWait(1);
    }
    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt04bBallXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(138) == 0) {
        stage_SetAnimation(PAL_JPNKOR(299, 295), -1, -2);
    } else {
        stage_SetAnimation(PAL_JPNKOR(297, 293), -1, -2);
    }
}

void actSt04bSolarXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(138) == 0) {
        if (ICO_JPNKOR) {
            stage_SetAnimation(297, -1, -2);
        }
        stage_SetAnimation(301, -1, -2);
        if (!ICO_JPNKOR) {
            stage_SetAnimation(305, -1, -2);
        }
    }
}

void actSt04bSekizoEvent(int x)
{
    volatile int local = x;
}

void actSt04bGirlWay(GObj *volatile self)
{
    long long buf[2];
    long long way[2];

    buf[0] = girlWayPos.d[0];
    buf[1] = girlWayPos.d[1];
    _SCPMoveCharactorByWay(girlGObj, 0, (float *)buf, 100.0f, 0);

    way[0] = girlWay2Pos.d[0];
    way[1] = girlWay2Pos.d[1];
    RequestStageChangeDirect(girlGObj, 19, way, 180);

    brainUnlockGirl();
}
