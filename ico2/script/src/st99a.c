#include "st99a.h"
#include "act.h"
#include "commonact.h"
#include "script.h"
#include "StageAnimation.h"
#include "typedef.h"
#include "main.h"

static void actSt17aTestChk(GObj *volatile self);

static ActMail explode_mes[2] = {{430}, {429}}; /* derived name */

static ActMail splash1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail splash2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail wave_mes[2] = {{430}, {429}}; /* derived name */

static ActMail st27aWave_mes[2] = {{430}, {429}}; /* derived name */

static ActMail spider_mes[2] = {{430}, {429}}; /* derived name */

static ActMail st17aTest_mes[2] = {{430}, {429}}; /* derived name */

void actExplode(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    explode_mes[0].func = actExplodeChk;
    act->mail = explode_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSplash1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    splash1_mes[0].func = actSplash1Chk;
    act->mail = splash1_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSplash2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    splash2_mes[0].func = actSplash2Chk;
    act->mail = splash2_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actWave(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    wave_mes[0].func = actWaveChk;
    act->mail = wave_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSpider(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    stage_SetLoopFlag(PAL_JPN(160, 156), 1);
    stage_SetAnimation(PAL_JPN(160, 156), 1, 0);

    spider_mes[0].func = actSpiderChk;
    act->mail = spider_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actDevilLightning(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    scpLinkBGAtoLayoutedTargetSkeltonWithLocalRotationFlag(PAL_JPN(3513, 3490), 0,
                                                           PAL_JPN(554, 550), 0);
}

void actQueenLightning(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    scpLinkBGAtoLayoutedTargetSkeltonWithLocalRotationFlag(PAL_JPN(3512, 3489), 0,
                                                           PAL_JPN(555, 551), 0);
}

void actSt17aTest(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    stage_SetAnimation(PAL_JPN(132, 128), 0, 0);

    st17aTest_mes[0].func = actSt17aTestChk;
    act->mail = st17aTest_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt27aWave(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    st27aWave_mes[0].func = actSt27aWaveChk;
    act->mail = st27aWave_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actExplodeChk(GObj *volatile self)
{
    GOBJ_SUB(scpSearchGobj(PAL_JPN(3014, 3010)))->ctrl.noStand = 1;
    GOBJ_SUB(scpSearchGobj(PAL_JPN(3014, 3010)))->ctrl.noStand = 0;
    scpGetWallCollision(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    _ACTWait(60);
    stage_SetAnimation(PAL_JPN(511, 507), 1, 0);
    scpLinkBGAtoLayoutedTarget(PAL_JPN(3014, 3010), PAL_JPN(511, 507));
    while (stage_CheckAnimationFinish(PAL_JPN(511, 507)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

void actSplash1Chk(GObj *volatile self)
{
    GOBJ_SUB(scpSearchGobj(PAL_JPN(3015, 3011)))->ctrl.noStand = 1;
    GOBJ_SUB(scpSearchGobj(PAL_JPN(3015, 3011)))->ctrl.noStand = 0;
    scpGetWallCollision(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    _ACTWait(60);
    stage_SetAnimation(PAL_JPN(498, 494), 1, 0);
    scpLinkBGAtoLayoutedTarget(PAL_JPN(3015, 3011), PAL_JPN(498, 494));
    while (stage_CheckAnimationFinish(PAL_JPN(498, 494)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

void actSplash2Chk(GObj *volatile self)
{
    GOBJ_SUB(scpSearchGobj(PAL_JPN(3016, 3012)))->ctrl.noStand = 1;
    GOBJ_SUB(scpSearchGobj(PAL_JPN(3016, 3012)))->ctrl.noStand = 0;
    scpGetWallCollision(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    _ACTWait(60);
    stage_SetAnimation(PAL_JPN(499, 495), 1, 0);
    scpLinkBGAtoLayoutedTarget(PAL_JPN(3016, 3012), PAL_JPN(499, 495));
    while (stage_CheckAnimationFinish(PAL_JPN(499, 495)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

void actWaveChk(GObj *volatile self)
{
    actCreateSubThread(actWave1, 21);
}

void actWave1(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    while (1) {
        stage_SetAnimation(PAL_JPN(519, 515), 1, 0);
        _ACTWait(179);
        stage_SetAnimation(PAL_JPN(520, 516), 1, 0);
        _ACTWait(179);
        stage_SetAnimation(PAL_JPN(521, 517), 1, 0);
        _ACTWait(179);
        stage_SetAnimation(PAL_JPN(522, 518), 1, 0);
        _ACTWait(179);
    }
}

void actSt27aWaveChk(GObj *volatile self)
{
    actCreateSubThread(actSt27aWave1, 21);
}

void actSt27aWave1(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    while (1) {
        stage_SetAnimation(PAL_JPN(194, 190), 1, 0);
        _ACTWait(200);
        stage_SetAnimation(PAL_JPN(195, 191), 1, 0);
        _ACTWait(200);
        stage_SetAnimation(PAL_JPN(196, 192), 1, 0);
        _ACTWait(200);
        stage_SetAnimation(PAL_JPN(197, 193), 1, 0);
        _ACTWait(200);
    }
}

void actSpiderChk(GObj *volatile self)
{
    while (1) {
        while ((GOBJ_ACT(boyGObj)->pad.trg & 0x400) == 0) {
            _ACTWait(1);
        }
        scpBornSpider(2, 0.0f, -500.0f, 0.0f, 500.0f);
        _ACTWait(1);
    }
}

static void actSt17aTestChk(GObj *volatile self)
{
    while (1) {
        while ((pad[1].flags & 0x20) == 0) {
            _ACTWait(1);
        }
        stage_SetAnimation(PAL_JPN(84, 80), 1, 0);
        _ACTWait(1);
    }
}
