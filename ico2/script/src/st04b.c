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

static ActMail sekizo_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail sekizo_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail sekizo_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail sekizo_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define sekizo_mes                                                                                 \
    PAL_NTSC_AUG6_JUN26(sekizo_mes_pal, sekizo_mes_ntsc, sekizo_mes_aug6, sekizo_mes_jun26)

static ActMail ene1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail ene1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail ene1_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail ene1_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define ene1_mes PAL_NTSC_AUG6_JUN26(ene1_mes_pal, ene1_mes_ntsc, ene1_mes_aug6, ene1_mes_jun26)

/* A 16-byte constant vector template: the float view carries the values,
   the long long view is the one the copy reads. */

static const ConstVec girlWayPos = {{-10750.0f, -2122.0f, 0.0f, 0.0f}}; /* derived name */

static const ConstVec girlWay2Pos = {{-139.0f, -177.0f, 1670.0f, 0.0f}}; /* derived name */

void actSt04bEnd(void)
{
    if (girlGObj != 0) {
        if (gflagChk(PAL_NTSCAUG6JUN26(138, 124)) != 0) {
            if (gflagChk(PAL_NTSCAUG6JUN26(157, 140)) == 0) {
                gflagOn(PAL_NTSC_AUG6JUN26(391, 358, 357));
            }
        }
    }
}

/* .sdata: the stone statue's stream handle, its shake and the shake's volume. */
SqEntry *sekizo4b = 0;

int sekizo_4b = 0;

unsigned char sekizo_4b_vol = 0;

static void actSt04bSekizoChk_jun26(GObj *volatile self)
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

    scpSekizouCheckPoint();

    scpAdpcmPlayRequestFunc(21, 0, 1, 1, 1);

    scpKillEnemyAll();
    scpMaskGeneratorAll();

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    brainLockGirl();

    gflagOn(135);
    _ACTWait(60);

    stage_SetAnimation(153, 1, 0);

    key = iosPadActRequest(boyPad, 9);
    sekizo_4b_vol = 128;
    iosPadActVolumeSet(sekizo_4b = key, 128);

    se = soundSeDefPlay(1167, 0, 0, 1);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 521);

    scpPlayPosSet(girlGObj, -10325.0f, -2150.0f, 0.0f);
    scpPlayPosSet(boyGObj, -10325.0f, -2150.0f, 100.0f);
    _ACTWait(1);
    sceVu0SubVector(v, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, v);
    scpBoyControlReadDisable = 1;
    sceVu0SubVector(v, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, v);

    scpPlayMot(girlGObj, 584);
    scpPlayWaitMotEnd(girlGObj);

    soundSeDefStop(se);

    while (stage_CheckAnimationFrame(153, 151, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(sekizo_4b);

    scpPlayMot(girlGObj, 521);
    scpPlayEnd(girlGObj);

    actCreateSubThread(actSt04bGirlWay, 21);

    _ACTWait(30);
    scpPlayMot(boyGObj, 254);
    scpPlayWaitMotEnd(boyGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
}

void actSt04bSekizoChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04bSekizoChk_jun26(self);
        return;
    }
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

    if (ICO_NTSC) {
        lt_switch_layout(51);
        scpBoyControlReadDisable = 1;
        brainLockGirl();

        scpKillEnemyOne(3453);
        scpMaskGeneratorAll();
    } else if (ICO_AUG6JUN26) {
    } else {
        lt_switch_layout(PAL_JPNKORCN(55, 52));
        scpBoyControlReadDisable = 1;
        brainLockGirl();

        scpKillEnemyOne(PAL_JPNKORCN(3757, 3734));
        scpMaskGeneratorAll();
    }

    scpAdpcmPlayRequestFunc(18, &sekizo4b, 1, 1, 1);
    while (sekizo4b == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSC) {
        gflagOn(357);
        _ACTWait(60);
    } else if (ICO_AUG6JUN26) {
        scpKillEnemyOne(3415);
        scpMaskGeneratorAll();
    } else {
        gflagOn(390);
    }

    if (ICO_NTSC) {
        stage_SetAnimation(170, 1, 0);
    } else if (ICO_AUG6JUN26) {
        gflagOn(356);

        lt_switch_layout(51);
        scpBoyControlReadDisable = 1;
        brainLockGirl();

        _ACTWait(60);

        stage_SetAnimation(169, 1, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(191, 187), 1, 0);
    }

    ReviveAllCarryableItemsWithNonSleepFrame(250);

    key = iosPadActRequest(boyPad, 9);
    sekizo_4b_vol = 128;
    iosPadActVolumeSet(sekizo_4b = key, 128);

    se = soundSeDefPlay(PAL_NTSCAUG6JUN26(1217, 1193), 0, 0, 1);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, PAL_NTSCAUG6JUN26(532, 514));

    scpPlayPosSet(girlGObj, -10325.0f, -2150.0f, 0.0f);
    scpPlayPosSet(boyGObj, -10325.0f, -2150.0f, PAL_AUG6JUN26(-100.0f, 100.0f));
    _ACTWait(1);
    sceVu0SubVector(v, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, v);
    scpBoyControlReadDisable = 1;
    sceVu0SubVector(v, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, v);

    scpSekizouCheckPoint();

    scpPlayMot(girlGObj, PAL_NTSC_AUG6JUN26(645, 572, 573));
    scpPlayWaitMotEnd(girlGObj);

    gflagOn(PAL_NTSCAUG6JUN26(159, 142));

    soundSeDefStop(se);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6JUN26(191, 187, 170, 169), 151, 0) ==
           0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(sekizo_4b);

    scpPlayMot(girlGObj, PAL_NTSCAUG6JUN26(532, 514));
    scpPlayEnd(girlGObj);

    actCreateSubThread(actSt04bGirlWay, 21);

    _ACTWait(30);
    scpPlayMot(boyGObj, PAL_NTSCAUG6JUN26(252, 247));
    scpPlayWaitMotEnd(boyGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    ScpCallCameraSetTarget(10793.0f, 2122.0f, 0.0f);

    if (!ICO_NTSCAUG6JUN26) {
        while (stage_CheckAnimationFinish(PAL_JPNKORCN(191, 187)) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        lt_switch_layout(PAL_JPNKORCN(54, 51));
        scpBoyControlReadDisable = 0;
    }
}

inline static void actSt04bEne1Chk_jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (gflagChk(117) == 0 || scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }
}

static void actSt04bEne1Chk_aug6(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (gflagChk(124) == 0 || scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    scpSleepEnemyOne(3415);

    gflagOff(357);

    _ACTWait(60);

    gflagOn(140);
    gflagOn(141);

    stage_SetAnimation(162, 1, 0);
    SetCameraFlag_LwsCutBack();
    while (stage_CheckAnimationFinish(162) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;

    scpWakeupEnemyOne(3415);
}

static void actSt04bEne1Chk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (gflagChk(124) == 0 || scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    scpSleepEnemyOne(3453);

    gflagOff(358);

    _ACTWait(60);

    gflagOn(140);
    gflagOn(141);

    stage_SetAnimation(163, 1, 0);
    SetCameraFlag_LwsCutBack();
    while (stage_CheckAnimationFinish(163) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;

    scpWakeupEnemyOne(3453);
}

void actSt04bEne1Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04bEne1Chk_jun26(self);
        return;
    }
    if (ICO_AUG6) {
        actSt04bEne1Chk_aug6(self);
        return;
    }
    if (ICO_NTSC) {
        actSt04bEne1Chk_ntsc(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (PAL_NTSCAUG6JUN26(gflagChk(138) == 0 || (scpTriggerFloorAttr(girlGObj, 0x1000000) == 0 &&
                                                    scpTriggerFloorAttr(boyGObj, 0x2000000) == 0),
                             gflagChk(124) == 0 || scpTriggerFloorAttr(girlGObj, 0x1000000) == 0)) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
    scpBoyControlReadDisable = 1;

    scpSleepEnemyOne(PAL_JPNKORCN_NTSC_AUG6JUN26(3757, 3734, 3453, 3415));

    gflagOff(PAL_NTSC_AUG6JUN26(391, 358, 357));

    _ACTWait(60);

    gflagOn(PAL_NTSCAUG6JUN26(157, 140));
    gflagOn(PAL_NTSCAUG6JUN26(158, 141));

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(184, 180, 163, 162), 1, 0);
    SetCameraFlag_LwsCutBack();
    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(184, 180, 163, 162)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));
    scpBoyControlReadDisable = 0;

    scpWakeupEnemyOne(PAL_JPNKORCN_NTSC_AUG6JUN26(3757, 3734, 3453, 3415));
}

void actSt04bCrest01XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(174, 155, 148)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(185, 181, 164, 163, 149), 0, 0);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(186, 182, 165, 164, 150), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(185, 181, 164, 163, 149), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(186, 182, 165, 164, 150), 0, -1);
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(1056, 968, 966, 919));
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(1057, 969, 967, 920));
    }
}

void actSt04bDoorXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(249, 245, 221, 220, 195), 0, 0);
}

void actSt04bMonyoDoorXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(174, 155, 148)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(250, 246, 222, 221, 196), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(250, 246, 222, 221, 196), 0, -1);
    }
}

static void actSt04bSekizo_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(135) == 0) {
        stage_SetAnimation(153, 0, 0);
        sekizo_mes[0].func = actSt04bSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    } else {
        stage_SetAnimation(153, 0, -1);
    }
}

void actSt04bSekizo(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04bSekizo_jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(159, 142)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(191, 187, 170, 169), 0, 0);
        sekizo_mes[0].func = actSt04bSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(191, 187, 170, 169), 0, -1);
        if (gflagChk(PAL_NTSCAUG6JUN26(174, 155)) == 0) {
            ScpCallCameraSetTarget(10793.0f, 2122.0f, 0.0f);
        }
    }
}

void actSt04bEne1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(157, 140, 133)) == 0) {
        ene1_mes[0].func = actSt04bEne1Chk;
        act->mail = ene1_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }
}

void actSt04bEnemy1(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);
    while (gflagChk(PAL_NTSCAUG6_JUN26(158, 141, 134)) == 0) {
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
    while (gflagChk(PAL_NTSCAUG6_JUN26(158, 141, 134)) == 0) {
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

    if (gflagChk(PAL_NTSCAUG6_JUN26(138, 124, 117)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(299, 295, 266, 265, 239), -1, -2);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(297, 293, 264, 263, 237), -1, -2);
    }
}

static void actSt04bSolarXL_ntsc(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(124) == 0) {
        stage_SetAnimation(268, -1, -2);
        stage_SetAnimation(272, -1, -2);
    }
}

static void actSt04bSolarXL_aug6jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_JUN26(124, 117)) == 0) {
        stage_SetAnimation(PAL_JUN26(267, 241), -1, -2);
        stage_SetAnimation(PAL_JUN26(271, 245), -1, -2);
    }
}

void actSt04bSolarXL(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04bSolarXL_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04bSolarXL_aug6jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(138) == 0) {
        if (ICO_JPNKORCN) {
            stage_SetAnimation(297, -1, -2);
        }
        stage_SetAnimation(301, -1, -2);
        if (!ICO_JPNKORCN) {
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

    if (ICO_NTSCAUG6JUN26) {
        lt_switch_layout(50);
        scpBoyControlReadDisable = 0;
    }
    brainUnlockGirl();
}
