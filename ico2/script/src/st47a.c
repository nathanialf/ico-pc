#include "st47a.h"
#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
#include "s_init.h"
#include "act.h"
#include "gobj_process.h"
#include "commonact.h"
#include "way_llf.h"
#include "brain.h"
#include "camera-root.h"
#include "generator.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "attackCheckBoundary.h"
#include "item.h"
#include "motionManager2.h"
#include <libvu0.h>
#include <string.h>
#include "typedef.h"
#include "script.h"
#include "main.h"

static void actSt47aHint2OnChk(GObj *volatile self);
static void actSt47aRopeSub(GObj *volatile self);

void actSt47aInit(void)
{
    if (gflagChk(47) != 0) {
        stage_SetAnimation(PAL_JPNKOR(166, 162), 0, 0);
        SetWayGroupActive(33, 1);
    } else {
        stage_SetAnimation(PAL_JPNKOR(165, 161), 0, 0);
        SetWayGroupActive(33, 0);
    }

    if (gflagChk(48) != 0) {
        stage_SetAnimation(PAL_JPNKOR(168, 164), 0, 0);
        SetWayGroupActive(34, 1);
    } else {
        stage_SetAnimation(PAL_JPNKOR(167, 163), 0, 0);
        SetWayGroupActive(34, 0);
    }
}

void actSt47aEnd(void)
{
    if (girlGObj != 0) {
        if (gflagChk(52) == 0) {
            gflagOn(391);
        }
    }
}

/* .sdata: the statue and wing stream handles and the statue's shake. */
SqEntry *sekizo47a = 0;

SqEntry *hane1up = 0;

SqEntry *hane2up = 0;

SqEntry *hane1down = 0;

SqEntry *hane2down = 0;

int sekizo_47a = 0;

unsigned char sekizo_47a_vol = 0;

void actSt47aSekizo1Chk(GObj *volatile self)
{
    /* the sound handle, which the sound subsystem owns; it is kept across
       the whole cutscene and read again for soundSeDefStop */
    volatile int se;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) == 75) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKOR(55, 52));
    scpBoyControlReadDisable = 1;
    brainLockGirl();

    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(19, &sekizo47a, 1, 1, 1);
    while (sekizo47a == 0) {
        _ACTWait(1);
    }

    gflagOn(390);

    stage_SetAnimation(PAL_JPNKOR(163, 159), 1, 0);

    scpKillEnemyAll();
    scpMaskGeneratorAll();

    ReviveAllCarryableItemsWithNonSleepFrame(300);

    sekizo_47a = iosPadActRequest(boyPad, 9);
    sekizo_47a_vol = 128;
    iosPadActVolumeSet(sekizo_47a, 128);

    se = soundSeDefPlay(1217, 0, 0, 1);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 532);

    scpPlayPosSet(girlGObj, 1750.0f, -272.0f, 0.0f);
    scpPlayPosSet(boyGObj, 1750.0f, -272.0f, 50.0f);

    _ACTWait(1);
    {
        Vec16 v;

        sceVu0SubVector(v.f, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
        scpPlayMotDir(girlGObj, v.f);
        scpBoyControlReadDisable = 1;
        sceVu0SubVector(v.f, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
        scpPlayMotDir(boyGObj, v.f);

        scpSekizouCheckPoint();

        scpPlayMot(girlGObj, 645);
        scpPlayWaitMotEnd(girlGObj);

        gflagOn(43);

        soundSeDefStop(se);
    }

    while (stage_CheckAnimationFrame(PAL_JPNKOR(163, 159), 151, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(sekizo_47a);

    scpPlayMot(girlGObj, 532);
    scpPlayEnd(girlGObj);

    actCreateSubThread(actSt47aGirlWay, 21);

    _ACTWait(30);

    scpPlayMot(boyGObj, 252);
    scpPlayWaitMotEnd(boyGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    ScpCallCameraSetTarget(-3000.0f, 272.0f, 0.0f);

    while (stage_CheckAnimationFinish(PAL_JPNKOR(163, 159)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKOR(54, 51));
}

/* the shared pad-state array (op.c's PadState, GsBase.c's GsbPad): 0x58 per
   pad, trg at 0x4. */

static ActMail sekizo1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hane1Main_mes[2] = {{406, actSt47aHane1Switch}, {429}}; /* derived name */

static ActMail hane1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hane1SwitchUp_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hane1SwitchDown_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hane1Down_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hane1Up_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hane2Main_mes[2] = {{407, actSt47aHane2Switch}, {429}}; /* derived name */

static ActMail hane2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hane2SwitchUp_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hane2SwitchDown_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hane2Down_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hane2Up_mes[2] = {{430}, {429}}; /* derived name */

static ActMail rope_mes[2] = {{430}, {429}}; /* derived name */

static ActMail barricade_mes[2] = {{430}, {429}}; /* derived name */

static ActMail exit_mes[2] = {{430}, {429}}; /* derived name */

static ActMail exit2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail ene_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hint2On_mes[2] = {{430}, {429}}; /* derived name */

void actSt47aHane1Down(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    scpAdpcmPlayRequestFunc(64, &hane1down, 1, 1, 1);

    while (hane1down == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JPNKOR(165, 161), 1, 0);

    gflagOn(47);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(PAL_JPNKOR(165, 161)) == 0) {
        if ((pad[0].flags & 0x800) != 0 && scpAdpcmPlayRequestNum() == 0) {
            scpFadeOut(16.0f, 0, 0, 0);

            scpAdpcmFadeCloseFunc(&hane1down, 512);

            while (scpFadeChk() != 0) {
                _ACTWait(1);
            }
            while (lt_fade_status() != 2) {
                _ACTWait(1);
            }

            stage_SetAnimation(PAL_JPNKOR(165, 161), 0, -1);

            scpFadeIn(3.0f);
            break;
        }
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 0;

    lt_switch_layout(PAL_JPNKOR(54, 51));
    scpWakeupEnemyAll();

    SetWayGroupActive(33, 1);

    hane1Down_mes[0].func = actSt47aHane1Main;
    act->mail = hane1Down_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt47aHane1Up(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    if (girlGObj != 0) {
        if (scpTriggerFloorAttr(girlGObj, 0x2000000) != 0) {
            actCreateSubThread(actSt47aHane1_1Girl, 21);
        }
        if (scpTriggerFloorAttr(girlGObj, 0x4000000) != 0) {
            actCreateSubThread(actSt47aHane1_2Girl, 21);
        }
    }

    scpAdpcmPlayRequestFunc(66, &hane1up, 1, 1, 1);

    while (hane1up == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JPNKOR(166, 162), 1, 0);

    gflagOff(47);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(PAL_JPNKOR(166, 162)) == 0) {
        if ((pad[0].flags & 0x800) != 0 && scpAdpcmPlayRequestNum() == 0) {
            scpFadeOut(16.0f, 0, 0, 0);

            scpAdpcmFadeCloseFunc(&hane1up, 512);

            while (scpFadeChk() != 0) {
                _ACTWait(1);
            }
            while (lt_fade_status() != 2) {
                _ACTWait(1);
            }

            stage_SetAnimation(PAL_JPNKOR(166, 162), 0, -1);

            scpFadeIn(3.0f);
            break;
        }
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 0;

    lt_switch_layout(PAL_JPNKOR(54, 51));
    scpWakeupEnemyAll();

    SetWayGroupActive(33, 0);

    hane1Up_mes[0].func = actSt47aHane1Main;
    act->mail = hane1Up_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt47aHane2Down(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    scpAdpcmPlayRequestFunc(65, &hane2down, 1, 1, 1);

    while (hane2down == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JPNKOR(167, 163), 1, 0);

    gflagOn(48);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(PAL_JPNKOR(167, 163)) == 0) {
        if ((pad[0].flags & 0x800) != 0 && scpAdpcmPlayRequestNum() == 0) {
            scpFadeOut(16.0f, 0, 0, 0);

            scpAdpcmFadeCloseFunc(&hane2down, 512);

            while (scpFadeChk() != 0) {
                _ACTWait(1);
            }
            while (lt_fade_status() != 2) {
                _ACTWait(1);
            }

            stage_SetAnimation(PAL_JPNKOR(167, 163), 0, -1);

            scpFadeIn(3.0f);
            break;
        }
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 0;

    lt_switch_layout(PAL_JPNKOR(54, 51));
    scpWakeupEnemyAll();

    SetWayGroupActive(34, 1);

    hane2Down_mes[0].func = actSt47aHane2Main;
    act->mail = hane2Down_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt47aHane2Up(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        actCreateSubThread(actSt47aHane2Girl, 21);
    }

    scpAdpcmPlayRequestFunc(67, &hane2up, 1, 1, 1);

    while (hane2up == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JPNKOR(168, 164), 1, 0);

    gflagOff(48);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(PAL_JPNKOR(168, 164)) == 0) {
        if ((pad[0].flags & 0x800) != 0 && scpAdpcmPlayRequestNum() == 0) {
            scpFadeOut(16.0f, 0, 0, 0);

            scpAdpcmFadeCloseFunc(&hane2up, 512);

            while (scpFadeChk() != 0) {
                _ACTWait(1);
            }
            while (lt_fade_status() != 2) {
                _ACTWait(1);
            }

            stage_SetAnimation(PAL_JPNKOR(168, 164), 0, -1);

            scpFadeIn(3.0f);
            break;
        }
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 0;

    lt_switch_layout(PAL_JPNKOR(54, 51));
    scpWakeupEnemyAll();

    SetWayGroupActive(34, 0);

    hane2Up_mes[0].func = actSt47aHane2Main;
    act->mail = hane2Up_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt47aRope(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(49) == 0) {
        stage_SetAnimation(PAL_JPNKOR(169, 165), 0, 0);
        scpSearchGobj(503)->active = 0;
        rope_mes[0].func = actSt47aRopeChk;
        act->mail = rope_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(170, 166), 0, -1);
        scpSearchGobj(503)->active = 1;
        scpSearchGobj(482)->active = 0;
        FinishHint(5);
    }
}

/* .sbss: the demo's own end flag, raised by the subthread the wait loop below
   spins for. */
static int demoEnd;

void actSt47aRopeChk(GObj *volatile self)
{
    GObj *x = self;
    GProc *th;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(49) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(482))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(PAL_JPNKOR(169, 165), 1, 0);
            while (stage_CheckAnimationFinish(PAL_JPNKOR(169, 165)) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(482)->active = 0;

            lt_switch_layout(PAL_JPNKOR(55, 52));
            scpBoyControlReadDisable = 1;

            gflagOn(49);
            FinishHint(5);

            scpSleepEnemyAll();

            th = actCreateSubThread(actSt47aRopeSub, 21);

            demoEnd = 0;
            while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
                _ACTWait(1);
            }

            iosThreadSetPri(&th->thread, 34);

            if (demoEnd == 0) {
                scpFadeOut(16.0f, 0, 0, 0);
                while (scpFadeChk() != 0) {
                    _ACTWait(1);
                }
                while (lt_fade_status() != 2) {
                    _ACTWait(1);
                }
                stage_SetAnimation(PAL_JPNKOR(170, 166), 0, -1);
                scpSearchGobj(503)->active = 1;
                scpFadeIn(3.0f);
            }

            scpWakeupEnemyAll();
            scpBoyControlReadDisable = 0;
            lt_switch_layout(PAL_JPNKOR(54, 51));
            break;
        }
    }
}

void actSt47aBarricadeChk(GObj *volatile self)
{
    GObj *n;

    while ((n = scpIsBombExplode(19)) == 0 || scpTriggerBall(self, n, 350.0f) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    lt_switch_layout(PAL_JPNKOR(55, 52));
    scpBoyControlReadDisable = 1;

    FinishHint(4);
    FinishHint(6);

    scpSleepEnemyAll();

    _ACTWait(5);

    stage_SetAnimation(PAL_JPNKOR(171, 167), 1, 0);

    gflagOn(50);

    while (stage_CheckAnimationFinish(PAL_JPNKOR(171, 167)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 3);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKOR(54, 51));

    scpWakeupEnemyAll();
}

void actSt47aEnemy1(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    Generator_Mask(scpSearchGobj(513));

    while (gflagChk(53) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);

    Generator_Call(scpSearchGobj(513));
}

void actSt47aTorch(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(153) != 0) {
        scpTorchLightOn(489);
        scpTorchLightOn(490);
        scpTorchLightOn(491);
        scpTorchLightOn(492);
        scpTorchLightOn(493);
        scpTorchLightOn(494);
        scpTorchLightOn(495);
        scpTorchLightOn(496);
        scpTorchLightOn(497);
        scpTorchLightOn(498);
        scpTorchLightOn(499);
        scpTorchLightOn(500);
        scpTorchLightOn(501);
        scpTorchLightOn(502);
    }
}

void actSt47aSekizo1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(43) == 0) {
        stage_SetAnimation(PAL_JPNKOR(163, 159), 0, 0);
        sekizo1_mes[0].func = actSt47aSekizo1Chk;
        act->mail = sekizo1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(163, 159), 0, -1);
        if (gflagChk(137) == 0) {
            ScpCallCameraSetTarget(-3000.0f, 272.0f, 0.0f);
        }
    }
}

void actSt47aSekizo2(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    scpSekizou(self, 44, PAL_JPNKOR(164, 160), 0, 18, -2450.0f, -1372.0f, -1150.0f, -2450.0f,
               -1372.0f, -1250.0f);
}

void actSt47aHane1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    gflagChk(47);

    hane1_mes[0].func = actSt47aHane1Main;
    act->mail = hane1_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt47aHane2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    hane2_mes[0].func = actSt47aHane2Main;
    act->mail = hane2_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt47aBarricade(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(50) == 0) {
        stage_SetAnimation(PAL_JPNKOR(171, 167), 0, 0);
        barricade_mes[0].func = actSt47aBarricadeChk;
        act->mail = barricade_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        FinishHint(4);
        FinishHint(6);
        stage_SetAnimation(PAL_JPNKOR(171, 167), 0, -1);
    }
}

void actSt47aExit(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    exit_mes[0].func = actSt47aExitChk;
    act->mail = exit_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt47aExit2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(144) != 0) {
        exit2_mes[0].func = actSt47aExit2Chk;
        act->mail = exit2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt47aEne(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(52) == 0) {
        ene_mes[0].func = actSt47aEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt47aEnemy2(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(53) == 0) {
        _ACTWait(1);
    }
    _ACTWait(180);

    Generator_MaskOff(self);
    Generator_Call(self);

    _ACTWait(60);

    Generator_Call(self);
}

void actSt47aEnemy3(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(53) == 0) {
        _ACTWait(1);
    }
    _ACTWait(180);

    Generator_MaskOff(self);
    Generator_Call(self);

    _ACTWait(60);

    Generator_Call(self);
}

void actSt47aEnemy4(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(53) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);
}

void actSt47aHint2On(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(54) == 0) {
        SleepHint(5);

        hint2On_mes[0].func = actSt47aHint2OnChk;
        act->mail = hint2On_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt47aSekizo1Event(int x)
{
    volatile int local = x;
}

/* A 16-byte constant vector template: the float view carries the values,
   the long long view is the one the copy reads. */

static const ConstVec girlWayPos = {{2266.0f, -272.0f, 0.0f, 0.0f}}; /* derived name */

static const ConstVec hane1_1GirlPos = {{-1076.0f, -1972.0f, 755.0f, 0.0f}}; /* derived name */

static const ConstVec hane1_2GirlPos = {{1028.0f, -1972.0f, 744.0f, 0.0f}}; /* derived name */

static const ConstVec hane2GirlPos = {{-1031.0f, -1972.0f, -747.0f, 0.0f}}; /* derived name */

void actSt47aGirlWay(GObj *volatile self)
{
    long long buf[2];
    long long way[2];

    buf[0] = girlWayPos.d[0];
    buf[1] = girlWayPos.d[1];
    _SCPMoveCharactorByWay(girlGObj, 0, (float *)buf, 100.0f, 0);

    memset(way, 0, 16);
    RequestStageChangeDirect(girlGObj, 11, way, 180);
    brainUnlockGirl();
}

void actSt47aSekizo2Event(int x)
{
    volatile int local = x;
}

void actSt47aHane1Main(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = hane1Main_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt47aHane1Switch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    lt_switch_layout(PAL_JPNKOR(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    if (gflagChk(47) != 0) {
        hane1SwitchUp_mes[0].func = actSt47aHane1Up;
        sub->mail = hane1SwitchUp_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }

    hane1SwitchDown_mes[0].func = actSt47aHane1Down;
    sub->mail = hane1SwitchDown_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt47aHane1_1Girl(GObj *volatile self)
{
    long long buf[2];
    buf[0] = hane1_1GirlPos.d[0];
    buf[1] = hane1_1GirlPos.d[1];
    _SCPMoveCharactorByWay(girlGObj, 0, (float *)buf, 100.0f, 0);
}

void actSt47aHane1_2Girl(GObj *volatile self)
{
    long long buf[2];
    buf[0] = hane1_2GirlPos.d[0];
    buf[1] = hane1_2GirlPos.d[1];
    _SCPMoveCharactorByWay(girlGObj, 0, (float *)buf, 100.0f, 0);
}

void actSt47aHane2Main(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = hane2Main_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt47aHane2Switch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    lt_switch_layout(PAL_JPNKOR(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    if (gflagChk(48) != 0) {
        hane2SwitchUp_mes[0].func = actSt47aHane2Up;
        sub->mail = hane2SwitchUp_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }

    hane2SwitchDown_mes[0].func = actSt47aHane2Down;
    sub->mail = hane2SwitchDown_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt47aHane2Girl(GObj *volatile self)
{
    long long buf[2];
    buf[0] = hane2GirlPos.d[0];
    buf[1] = hane2GirlPos.d[1];
    _SCPMoveCharactorByWay(girlGObj, 0, (float *)buf, 100.0f, 0);
}

static void actSt47aRopeSub(GObj *volatile self)
{
    _ACTWait(15);

    stage_SetAnimation(PAL_JPNKOR(170, 166), 1, 0);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(170, 166), 148, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpSearchGobj(503)->active = 1;

    while (stage_CheckAnimationFinish(PAL_JPNKOR(170, 166)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

void actSt47aBarricadeEvent(int x)
{
    volatile int local = x;
}

void actSt47aExitChk(GObj *volatile self)
{
    scpSearchGobj(481)->active = 0;

    while ((gflagChk(174) == 0) || (gflagChk(234) == 0)) {
        _ACTWait(1);
    }

    gflagOn(51);
    scpSearchGobj(480)->active = 0;
    scpSearchGobj(481)->active = 1;
}

void actSt47aExit2Chk(GObj *volatile self)
{
    scpSearchGobj(480)->active = 1;
    scpSearchGobj(481)->active = 0;
}

void actSt47aEneChk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    gflagOff(391);
    _ACTWait(1);
    gflagOn(52);
    gflagOn(53);
}

static void actSt47aHint2OnChk(GObj *volatile self)
{
    while (gflagChk(47) == 0) {
        _ACTWait(1);
    }

    gflagOn(54);
    WakeupHint(5);
}
