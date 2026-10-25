#include "st05b.h"
#include "layout_texture.h"
#include "pad.h"
#include "s_init.h"
#include "act.h"
#include "commonact.h"
#include "brain.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "item.h"
#include <libvu0.h>
#include "typedef.h"
#include "main.h"
#include "script.h"

static ActMail sekizo_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail sekizo_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail sekizo_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail sekizo_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define sekizo_mes                                                                                 \
    PAL_NTSC_AUG6_JUN26(sekizo_mes_pal, sekizo_mes_ntsc, sekizo_mes_aug6, sekizo_mes_jun26)

static void actSt05bCrest01XL_jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    if (gflagChk(209) == 0) {
        stage_SetAnimation(151, 0, 0);
        stage_SetAnimation(152, 0, 0);
    } else {
        stage_SetAnimation(151, 0, -1);
        stage_SetAnimation(152, 0, -1);
        scpTorchLightOn(1185);
        scpTorchLightOn(1186);
    }
}

void actSt05bCrest01XL(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt05bCrest01XL_jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    if (gflagChk(PAL_NTSCAUG6JUN26(243, 218)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(187, 183, 166, 165), 0, 0);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(189, 185, 168, 167), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(187, 183, 166, 165), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(189, 185, 168, 167), 0, -1);
        scpTorchLightOn(PAL_NTSC_AUG6JUN26(1343, 1243, 1239));
        scpTorchLightOn(PAL_NTSC_AUG6JUN26(1344, 1244, 1240));
    }
    if (gflagChk(PAL_NTSCAUG6JUN26(244, 219)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(188, 184, 167, 166), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(188, 184, 167, 166), 0, -1);
        scpTorchLightOn(PAL_NTSC_AUG6JUN26(1345, 1245, 1241));
        scpTorchLightOn(PAL_NTSC_AUG6JUN26(1346, 1246, 1242));
    }
    if (gflagChk(PAL_NTSCAUG6JUN26(251, 226)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(190, 186, 169, 168), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(190, 186, 169, 168), 0, -1);
    }
}

/* .sdata: the stone statue's stream handle, its shake and the shake's volume. */
SqEntry *sekizo5b = 0;

int sekizo_5b = 0;

unsigned char sekizo_5b_vol = 0;

static void actSt05bSekizoChk_ntsc(GObj *volatile self)
{
    volatile int h;
    float d[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    brainLockGirl();
    scpKillEnemyAll();
    scpMaskGeneratorAll();
    scpAdpcmPlayRequestFunc(18, &sekizo5b, 1, 1, 1);
    while (sekizo5b == 0) {
        _ACTWait(1);
    }
    gflagOn(357);
    _ACTWait(60);
    stage_SetAnimation(171, 1, 0);
    ReviveAllCarryableItemsWithNonSleepFrame(250);
    sekizo_5b = iosPadActRequest(boyPad, 9);
    sekizo_5b_vol = 128;
    iosPadActVolumeSet(sekizo_5b, 128);
    h = soundSeDefPlay(1193, 0, 0, 1);
    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);
    scpPlayPosSet(girlGObj, 10350.0f, -2150.0f, 0.0f);
    scpPlayPosSet(boyGObj, 10350.0f, -2150.0f, -100.0f);
    _ACTWait(1);
    sceVu0SubVector(d, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, d);
    scpBoyControlReadDisable = 1;
    sceVu0SubVector(d, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, d);
    scpSekizouCheckPoint();
    scpPlayMot(girlGObj, 572);
    scpPlayWaitMotEnd(girlGObj);
    gflagOn(143);
    soundSeDefStop(h);
    while (stage_CheckAnimationFrame(171, 151, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActStop(sekizo_5b);
    scpPlayMot(girlGObj, 514);
    scpPlayEnd(girlGObj);
    actCreateSubThread(actSt05bGirlWay, 21);
    _ACTWait(30);
    scpPlayMot(boyGObj, 247);
    scpPlayWaitMotEnd(boyGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    ScpCallCameraSetTarget(-10793.0f, 2122.0f, 0.0f);
}

static void actSt05bSekizoChk_jun26(GObj *volatile self)
{
    volatile int h;
    float d[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0) {
        _ACTWait(1);
    }
    scpSekizouCheckPoint();
    scpAdpcmPlayRequestFunc(21, 0, 1, 1, 1);
    scpKillEnemyAll();
    scpMaskGeneratorAll();
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    brainLockGirl();
    gflagOn(136);
    _ACTWait(60);
    stage_SetAnimation(154, 1, 0);
    sekizo_5b = iosPadActRequest(boyPad, 9);
    sekizo_5b_vol = 128;
    iosPadActVolumeSet(sekizo_5b, 128);
    h = soundSeDefPlay(1167, 0, 0, 1);
    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 521);
    scpPlayPosSet(girlGObj, 10350.0f, -2150.0f, 0.0f);
    scpPlayPosSet(boyGObj, 10350.0f, -2150.0f, 100.0f);
    _ACTWait(1);
    sceVu0SubVector(d, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, d);
    scpBoyControlReadDisable = 1;
    sceVu0SubVector(d, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, d);
    scpPlayMot(girlGObj, 584);
    scpPlayWaitMotEnd(girlGObj);
    soundSeDefStop(h);
    while (stage_CheckAnimationFrame(154, 151, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActStop(sekizo_5b);
    scpPlayMot(girlGObj, 521);
    scpPlayEnd(girlGObj);
    actCreateSubThread(actSt05bGirlWay, 21);
    _ACTWait(30);
    scpPlayMot(boyGObj, 254);
    scpPlayWaitMotEnd(boyGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
}

static void actSt05bSekizoChk_aug6(GObj *volatile self)
{
    volatile int h;
    float d[4];
    int key;

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0) {
        _ACTWait(1);
    }
    scpAdpcmPlayRequestFunc(18, &sekizo5b, 1, 1, 1);
    while (sekizo5b == 0) {
        _ACTWait(1);
    }
    scpKillEnemyAll();
    scpMaskGeneratorAll();
    gflagOn(356);
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    brainLockGirl();
    _ACTWait(60);
    stage_SetAnimation(170, 1, 0);
    ReviveAllCarryableItemsWithNonSleepFrame(250);
    key = iosPadActRequest(boyPad, 9);
    sekizo_5b_vol = 128;
    iosPadActVolumeSet(sekizo_5b = key, 128);
    h = soundSeDefPlay(1193, 0, 0, 1);
    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);
    scpPlayPosSet(girlGObj, 10350.0f, -2150.0f, 0.0f);
    scpPlayPosSet(boyGObj, 10350.0f, -2150.0f, 100.0f);
    _ACTWait(1);
    sceVu0SubVector(d, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, d);
    scpBoyControlReadDisable = 1;
    sceVu0SubVector(d, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, d);
    scpSekizouCheckPoint();
    scpPlayMot(girlGObj, 573);
    scpPlayWaitMotEnd(girlGObj);
    gflagOn(143);
    soundSeDefStop(h);
    while (stage_CheckAnimationFrame(170, 151, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActStop(sekizo_5b);
    scpPlayMot(girlGObj, 514);
    scpPlayEnd(girlGObj);
    actCreateSubThread(actSt05bGirlWay, 21);
    _ACTWait(30);
    scpPlayMot(boyGObj, 247);
    scpPlayWaitMotEnd(boyGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    ScpCallCameraSetTarget(-10793.0f, 2122.0f, 0.0f);
}

void actSt05bSekizoChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt05bSekizoChk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt05bSekizoChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt05bSekizoChk_aug6(self);
        return;
    }
    volatile int h;
    float d[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    brainLockGirl();
    scpKillEnemyAll();
    scpMaskGeneratorAll();
    scpAdpcmPlayRequestFunc(18, &sekizo5b, 1, 1, 1);
    while (sekizo5b == 0) {
        _ACTWait(1);
    }
    gflagOn(390);
    stage_SetAnimation(PAL_JPNKORCN(192, 188), 1, 0);
    ReviveAllCarryableItemsWithNonSleepFrame(250);
    sekizo_5b = iosPadActRequest(boyPad, 9);
    sekizo_5b_vol = 128;
    iosPadActVolumeSet(sekizo_5b, 128);
    h = soundSeDefPlay(1217, 0, 0, 1);
    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 532);
    scpPlayPosSet(girlGObj, 10350.0f, -2150.0f, 0.0f);
    scpPlayPosSet(boyGObj, 10350.0f, -2150.0f, -100.0f);
    _ACTWait(1);
    sceVu0SubVector(d, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, d);
    scpBoyControlReadDisable = 1;
    sceVu0SubVector(d, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, d);
    scpSekizouCheckPoint();
    scpPlayMot(girlGObj, 645);
    scpPlayWaitMotEnd(girlGObj);
    gflagOn(160);
    soundSeDefStop(h);
    while (stage_CheckAnimationFrame(PAL_JPNKORCN(192, 188), 151, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActStop(sekizo_5b);
    scpPlayMot(girlGObj, 532);
    scpPlayEnd(girlGObj);
    actCreateSubThread(actSt05bGirlWay, 21);
    _ACTWait(30);
    scpPlayMot(boyGObj, 252);
    scpPlayWaitMotEnd(boyGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    ScpCallCameraSetTarget(-10793.0f, 2122.0f, 0.0f);
    while (stage_CheckAnimationFinish(PAL_JPNKORCN(192, 188)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpBoyControlReadDisable = 0;
}

void actSt05bDoorXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(243, 218, 209)) != 0 &&
        gflagChk(PAL_NTSCAUG6_JUN26(139, 125, 118)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(345, 341, 312, 311, 279), 0, -1);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(345, 341, 312, 311, 279), 0, 0);
    }
}

void actSt05bMonyoDoorXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(234, 209, 200)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(251, 247, 223, 222, 197), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(251, 247, 223, 222, 197), 0, -1);
    }
}

static void actSt05bSekizo_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(136) == 0) {
        stage_SetAnimation(154, 0, 0);
        sekizo_mes[0].func = actSt05bSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    } else {
        stage_SetAnimation(154, 0, -1);
    }
}

void actSt05bSekizo(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt05bSekizo_jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(160, 143)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(192, 188, 171, 170), 0, 0);
        sekizo_mes[0].func = actSt05bSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(192, 188, 171, 170), 0, -1);
        if (gflagChk(PAL_NTSCAUG6JUN26(243, 218)) == 0) {
            ScpCallCameraSetTarget(-10793.0f, 2122.0f, 0.0f);
        }
    }
}

void actSt05bBallXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(139, 125, 118)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(300, 296, 267, 266, 240), -1, -2);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(298, 294, 265, 264, 238), -1, -2);
    }
}

void actSt05bSolarXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(139, 125, 118)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(303, 299, 270, 269, 243), -1, -2);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(306, 302, 273, 272, 246), -1, -2);
    }
}

void actSt05bSekizoEvent(int x)
{
    volatile int local = x;
}

/* A 16-byte constant vector template: the float view carries the values,
   the long long view is the one the copy reads. */

static const ConstVec girlWayPos = {{10750.0f, -2122.0f, 0.0f, 0.0f}}; /* derived name */

static const ConstVec girlWay2Pos = {{139.0f, -177.0f, 1670.0f, 0.0f}}; /* derived name */

void actSt05bGirlWay(GObj *volatile self)
{
    long long buf[2];
    long long way[2];

    buf[0] = girlWayPos.d[0];
    buf[1] = girlWayPos.d[1];
    _SCPMoveCharactorByWay(girlGObj, 0, (float *)buf, 100.0f, 0);

    way[0] = girlWay2Pos.d[0];
    way[1] = girlWay2Pos.d[1];
    RequestStageChangeDirect(girlGObj, 28, way, 180);
    if (ICO_NTSCAUG6JUN26) {
        lt_switch_layout(50);
        scpBoyControlReadDisable = 0;
    }
    brainUnlockGirl();
}
