#include "st05c.h"
#include "layout_texture.h"
#include "thread.h"
#include "s_init.h"
#include "act.h"
#include "gobj_process.h"
#include "commonact.h"
#include "generator.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "attackCheckBoundary.h"
#include "st04c.h"
#include "typedef.h"
#include "script.h"
#include "main.h"

static void actSt04rDoor2Sub(GObj *volatile self);
static void actSt04rDoorSub(GObj *volatile self);
static void actSt05cCrestHintChk(GObj *volatile self);

/* .sbss: the demo's own end flag, raised by the subthreads the wait loops
   below spin for. */
static int demoEnd;

static const StVec doorDownChkPos = {{0.0f, 84.0f, -1359.0f, 0.0f}}; /* derived name */

static const StVec doorDownEffectPos = {{0.0f, 50.0f, -1450.0f, 1.0f}}; /* derived name */

static const StVec doorDownEffect2Pos = {{-2.0f, 250.0f, -1450.0f, 1.0f}}; /* derived name */

static const StVec doorDownEffect3Pos = {{5.0f, 260.0f, -1450.0f, 1.0f}}; /* derived name */

static ActMail doorDown_mes[2] = {{430}, {429}}; /* derived name */

static ActMail ene_mes[2] = {{430}, {429}}; /* derived name */

static ActMail st04rDoor_mes[2] = {{430}, {429}}; /* derived name */

static ActMail st04rDoor2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail crestHint_mes[2] = {{430}, {429}}; /* derived name */

void actSt05cDoorDownChk(GObj *volatile self)
{
    StVec pos;

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    _ACTWait(30);
    gflagOff(390);
    actCreateSubThread(actSt05cDoorDownEffect, 21);
    stage_SetAnimation(PAL_JPNKOR(347, 343), 1, 0);

    pos = doorDownChkPos;
    _ACTWait(30);
    soundSeDefPlay(1221, 0, pos.f, 1);
    _ACTWait(30);
    soundSeDefPlay(1222, 0, pos.f, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKOR(347, 343)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    gflagOn(165);
    scpBoyControlReadDisable = 0;
}

void actSt04rDoorChk(GObj *volatile self)
{
    GObj *x = self;
    GProc *th;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(255) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1380))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(PAL_JPNKOR(334, 330), 1, 0);
            while (stage_CheckAnimationFinish(PAL_JPNKOR(334, 330)) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1380)->active = 0;
            lt_switch_layout(PAL_JPNKOR(55, 52));
            scpBoyControlReadDisable = 1;
            scpSleepEnemyAll();
            gflagOn(255);
            demoEnd = 0;
            th = actCreateSubThread(actSt04rDoorSub, 21);

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
                stage_SetAnimation(PAL_JPNKOR(332, 328), 0, -1);
                scpFadeIn(3.0f);
            }

            soundSeDefPlay(1331, 0, 0, 1);
            scpWakeupEnemyAll();
            scpBoyControlReadDisable = 0;
            lt_switch_layout(PAL_JPNKOR(54, 51));
            break;
        }
    }
}

void actSt04rDoor2Chk(GObj *volatile self)
{
    GObj *x = self;
    GProc *th;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(256) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1381))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(PAL_JPNKOR(335, 331), 1, 0);
            while (stage_CheckAnimationFinish(PAL_JPNKOR(335, 331)) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1381)->active = 0;
            lt_switch_layout(PAL_JPNKOR(55, 52));
            scpBoyControlReadDisable = 1;
            scpSleepEnemyAll();
            gflagOn(256);
            demoEnd = 0;
            th = actCreateSubThread(actSt04rDoor2Sub, 21);

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
                stage_SetAnimation(PAL_JPNKOR(333, 329), 0, -1);
                scpFadeIn(3.0f);
            }

            soundSeDefPlay(1331, 0, 0, 1);
            scpWakeupEnemyAll();
            scpBoyControlReadDisable = 0;
            lt_switch_layout(PAL_JPNKOR(54, 51));
            break;
        }
    }
}

void actSt05cSolarXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(139) == 0) {
        stage_SetAnimation(PAL_JPNKOR(304, 300), -1, -2);
    }
}

void actSt05cWaterXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(231) != 0) {
        scpSearchGobj(1376)->active = 0;
    }
}

void actSt04rDoor(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(255) == 0) {
        stage_SetAnimation(PAL_JPNKOR(332, 328), 0, 0);
        st04rDoor_mes[0].func = actSt04rDoorChk;
        act->mail = st04rDoor_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(332, 328), 0, -1);
        scpSearchGobj(1380)->active = 0;
    }
}

void actSt04rDoor2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(256) == 0) {
        stage_SetAnimation(PAL_JPNKOR(333, 329), 0, 0);
        st04rDoor2_mes[0].func = actSt04rDoor2Chk;
        act->mail = st04rDoor2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(333, 329), 0, -1);
        scpSearchGobj(1381)->active = 0;
    }
}

void actSt05cDoorDown(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    if (gflagChk(165) == 0) {
        doorDown_mes[0].func = actSt05cDoorDownChk;
        act->mail = doorDown_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt05cEne(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(167) == 0) {
        ene_mes[0].func = actSt05cEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt05cEnemy1(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(168) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    Generator_MaskOff(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt05cEnemy2(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(168) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    Generator_MaskOff(self);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt05cCrestHint(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(169) == 0) {
        SleepHint(23);
        crestHint_mes[0].func = actSt05cCrestHintChk;
        act->mail = crestHint_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt05cDoorDownEvent(int x)
{
    volatile int local = x;
}

void actSt05cDoorDownEffect(GObj *volatile self)
{
    StVec a;
    StVec b;
    StVec c;
    int i;

    for (i = 0; i < 50; i++) {
        switch (i) {
        case 0:
            a = doorDownEffectPos;
            scpEffectStart(&a, 0);
            break;
        case 30:
            b = doorDownEffect2Pos;
            scpEffectStart(&b, 0);
            c = doorDownEffect3Pos;
            scpEffectStart(&c, 0);
            break;
        }
        _ACTWait(1);
    }
}

void actSt05cEneChk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (girlGObj == 0 || gflagChk(255) == 0) {
        _ACTWait(1);
    }

    _ACTWait(600);
    gflagOn(167);
    gflagOn(168);
}

static void actSt04rDoorSub(GObj *volatile self)
{
    int h;

    stage_SetAnimation(PAL_JPNKOR(332, 328), 1, 0);
    h = soundSeDefPlay(1330, 0, 0, 1);
    _ACTWait(90);
    soundSeDefStop(h);
    demoEnd = 1;
    _ACTWait(0);
}

static void actSt04rDoor2Sub(GObj *volatile self)
{
    int h;

    stage_SetAnimation(PAL_JPNKOR(333, 329), 1, 0);
    h = soundSeDefPlay(1330, 0, 0, 1);
    _ACTWait(90);
    soundSeDefStop(h);
    demoEnd = 1;
    _ACTWait(0);
}

static void actSt05cCrestHintChk(GObj *volatile self)
{
    while (gflagChk(243) == 0 || gflagChk(244) == 0 || gflagChk(245) == 0 || gflagChk(232) != 0) {
        _ACTWait(1);
    }

    gflagOn(169);
    WakeupHint(23);
}
