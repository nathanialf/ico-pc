#include "st04c.h"
#include "layout_texture.h"
#include "thread.h"
#include "s_init.h"
#include "commonact.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "attackCheckBoundary.h"
#include "act.h"
#include "gobj_process.h"
#include "typedef.h"
#include "generator.h"
#include "script.h"
#include "main.h"

static void actSt04cIntroChkSub(GObj *volatile self);

void actSt04cEnd(void)
{
    if (girlGObj != 0) {
        if (gflagChk(PAL_NTSCAUG6(163, 146)) == 0) {
            gflagOn(PAL_NTSC_AUG6(391, 358, 357));
        }
    }
}

/* A 16-byte constant vector template: the float view carries the values,
   the long long view is the one the copy reads. */

static const StVec doorDownChkPos = {{0.0f, 84.0f, -1359.0f, 0.0f}}; /* derived name */

static const ConstVec doorDownEffectPos = {{0.0f, 50.0f, -1450.0f, 1.0f}}; /* derived name */

static const ConstVec doorDownEffect2Pos = {{-2.0f, 250.0f, -1450.0f, 1.0f}}; /* derived name */

static const ConstVec doorDownEffect3Pos = {{5.0f, 260.0f, -1450.0f, 1.0f}}; /* derived name */

void actSt04cDoorDownChk(GObj *volatile self)
{
    StVec pos;

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    _ACTWait(30);

    actCreateSubThread(actSt04cDoorDownEffect, 21);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(253, 249, 225, 224), 1, 0);

    pos = doorDownChkPos;

    _ACTWait(30);
    soundSeDefPlay(PAL_NTSCAUG6(1221, 1197), 0, pos.f, 1);
    _ACTWait(30);
    soundSeDefPlay(PAL_NTSCAUG6(1222, 1198), 0, pos.f, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6(253, 249, 225, 224)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    gflagOn(PAL_NTSCAUG6(162, 145));
}

/* .sbss: the demo's own end flag, raised by the subthread the wait loop below
   spins for. */
static int demoEnd;

/* .sdata: the stream handle actSt04cIntroChk fades out when the intro ends (the 2001 builds') */
SqEntry *intro = 0;

static void actSt04cIntroChk_ntsc(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 1000.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    gflagOff(357);

    gflagOn(144);
    scpBoyControlReadDisable = 1;

    stage_SetAnimation(319, 1, 0);

    while (stage_CheckAnimationFinish(319) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (intro != 0) {
        scpAdpcmFadeClose(intro, 80);
    }

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
}

static void actSt04cIntroChk_aug6(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 1000.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    gflagOff(356);

    gflagOn(144);
    scpBoyControlReadDisable = 1;

    stage_SetAnimation(318, 1, 0);

    while (stage_CheckAnimationFinish(318) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (intro != 0) {
        scpAdpcmFadeClose(intro, 80);
    }

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
}

void actSt04cIntroChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04cIntroChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt04cIntroChk_aug6(self);
        return;
    }
    GProc *th;

    while (scpTriggerBall(self, boyGObj, 1000.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));

    gflagOff(390);

    gflagOn(161);
    scpBoyControlReadDisable = 1;

    stage_SetAnimation(PAL_JPNKORCN(352, 348), 1, 0);

    th = actCreateSubThread(actSt04cIntroChkSub, 21);
    demoEnd = 0;

    while (demoEnd == 0 && (!(pad[0].flags & 0x800) || scpAdpcmPlayRequestNum() != 0)) {
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
        stage_SetAnimation(PAL_JPNKORCN(352, 348), 0, -1);
        scpFadeIn(3.0f);
    }

    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpBoyControlReadDisable = 0;
}

static void actSt04lDoorChk_ntsc(GObj *volatile self)
{
    GObj *x = self;
    int h;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(179) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1010))) {
        case 0:
            _ACTWait(1);
            break;

        case 1:
            stage_SetAnimation(204, 1, 0);
            while (stage_CheckAnimationFinish(204) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;

        case 2:
            scpSearchGobj(1010)->active = 0;

            lt_switch_layout(51);

            scpBoyControlReadDisable = 1;
            scpSleepEnemyAll();

            gflagOn(179);

            stage_SetAnimation(203, 1, 0);

            h = soundSeDefPlay(1306, 0, 0, 1);

            _ACTWait(90);

            soundSeDefStop(h);

            soundSeDefPlay(1307, 0, 0, 1);

            scpWakeupEnemyAll();
            scpBoyControlReadDisable = 0;

            lt_switch_layout(50);
            break;
        }
    }
}

static void actSt04lDoorChk_aug6(GObj *volatile self)
{
    GObj *x = self;
    int h;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(179) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1007))) {
        case 0:
            _ACTWait(1);
            break;

        case 1:
            stage_SetAnimation(203, 1, 0);
            while (stage_CheckAnimationFinish(203) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;

        case 2:
            scpSearchGobj(1007)->active = 0;

            lt_switch_layout(51);

            scpBoyControlReadDisable = 1;
            scpSleepEnemyAll();

            gflagOn(179);

            stage_SetAnimation(202, 1, 0);

            h = soundSeDefPlay(1306, 0, 0, 1);

            _ACTWait(90);

            soundSeDefStop(h);

            soundSeDefPlay(1307, 0, 0, 1);

            scpWakeupEnemyAll();
            scpBoyControlReadDisable = 0;

            lt_switch_layout(50);
            break;
        }
    }
}

void actSt04lDoorChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lDoorChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt04lDoorChk_aug6(self);
        return;
    }
    GObj *x = self;
    int h;
    int i;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(199) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1098))) {
        case 0:
            _ACTWait(1);
            break;

        case 1:
            stage_SetAnimation(PAL_JPNKORCN(232, 228), 1, 0);
            while (stage_CheckAnimationFinish(PAL_JPNKORCN(232, 228)) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;

        case 2:
            scpSearchGobj(1098)->active = 0;
            FinishHint(15);

            lt_switch_layout(PAL_JPNKORCN(55, 52));

            scpBoyControlReadDisable = 1;
            scpSleepEnemyAll();

            gflagOn(199);

            stage_SetAnimation(PAL_JPNKORCN(231, 227), 1, 0);

            h = soundSeDefPlay(1330, 0, 0, 1);

            for (i = 90; i-- > 0;) {
                if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
                    scpFadeOut(16.0f, 0, 0, 0);
                    while (scpFadeChk() != 0) {
                        _ACTWait(1);
                    }
                    while (lt_fade_status() != 2) {
                        _ACTWait(1);
                    }
                    stage_SetAnimation(PAL_JPNKORCN(231, 227), 0, -1);
                    scpFadeIn(3.0f);
                    break;
                }
                _ACTWait(1);
            }

            soundSeDefStop(h);

            soundSeDefPlay(1331, 0, 0, 1);

            scpWakeupEnemyAll();
            scpBoyControlReadDisable = 0;

            lt_switch_layout(PAL_JPNKORCN(54, 51));
            break;
        }
    }
}

void actSt04cSolarXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6(138, 124)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(302, 298, 269, 268), -1, -2);
    }
}

static ActMail doorDown_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail doorDown_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail doorDown_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define doorDown_mes PAL_NTSC_AUG6(doorDown_mes_pal, doorDown_mes_ntsc, doorDown_mes_aug6)

static ActMail ene_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail ene_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail ene_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define ene_mes PAL_NTSC_AUG6(ene_mes_pal, ene_mes_ntsc, ene_mes_aug6)

static ActMail intro_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail intro_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail intro_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define intro_mes PAL_NTSC_AUG6(intro_mes_pal, intro_mes_ntsc, intro_mes_aug6)

static ActMail st04lDoor_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail st04lDoor_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail st04lDoor_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define st04lDoor_mes PAL_NTSC_AUG6(st04lDoor_mes_pal, st04lDoor_mes_ntsc, st04lDoor_mes_aug6)

static void actSt04lDoor_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(179) == 0) {
        stage_SetAnimation(203, 0, 0);
        st04lDoor_mes[0].func = actSt04lDoorChk;
        act->mail = st04lDoor_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        stage_SetAnimation(203, 0, -1);
        scpSearchGobj(1010)->active = 0;
    }
}

static void actSt04lDoor_aug6(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(179) == 0) {
        stage_SetAnimation(202, 0, 0);
        st04lDoor_mes[0].func = actSt04lDoorChk;
        act->mail = st04lDoor_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    } else {
        stage_SetAnimation(202, 0, -1);
        scpSearchGobj(1007)->active = 0;
    }
}

void actSt04lDoor(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lDoor_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt04lDoor_aug6(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(199) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(231, 227), 0, 0);
        st04lDoor_mes[0].func = actSt04lDoorChk;
        act->mail = st04lDoor_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(231, 227), 0, -1);
        scpSearchGobj(1098)->active = 0;
        FinishHint(15);
    }
}

void actSt04cIntro(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6(161, 144)) == 0) {
        intro_mes[0].func = actSt04cIntroChk;
        act->mail = intro_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt04cDoorDown(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    if (gflagChk(PAL_NTSCAUG6(162, 145)) == 0) {
        doorDown_mes[0].func = actSt04cDoorDownChk;
        act->mail = doorDown_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt04cEne(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6(163, 146)) == 0) {
        ene_mes[0].func = actSt04cEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt04cEnemy1(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);
    while (gflagChk(PAL_NTSCAUG6(164, 147)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt04cEnemy2(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);
    while (gflagChk(PAL_NTSCAUG6(164, 147)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt04cWaterXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6(230, 205)) != 0) {
        scpSearchGobj(PAL_NTSC_AUG6(1094, 1006, 1003))->active = 0;
    }
}

void actSt04cDoorDownEvent(int x)
{
    volatile int local = x;
}

void actSt04cDoorDownEffect(GObj *volatile self)
{
    long long b1[2];
    long long b2[2];
    long long b3[2];
    long long v0a = doorDownEffectPos.d[0];
    long long v0b = doorDownEffect2Pos.d[0];
    long long v0c = doorDownEffect3Pos.d[0];
    int i;
    for (i = 0; i < 50; i++) {
        switch (i) {
        case 0:
            b1[0] = v0a;
            b1[1] = doorDownEffectPos.d[1];
            scpEffectStart((int *)b1, 0);
            break;
        case 30:
            b2[0] = v0b;
            b2[1] = doorDownEffect2Pos.d[1];
            scpEffectStart((int *)b2, 0);
            b3[0] = v0c;
            b3[1] = doorDownEffect3Pos.d[1];
            scpEffectStart((int *)b3, 0);
            break;
        }
        _ACTWait(1);
    }
}

void actSt04cEneChk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerFloorAttr(girlGObj, 0xB000000) == 0) {
        _ACTWait(1);
    }
    gflagOff(PAL_NTSC_AUG6(391, 358, 357));
    _ACTWait(1);
    gflagOn(PAL_NTSCAUG6(163, 146));
    gflagOn(PAL_NTSCAUG6(164, 147));
}

static void actSt04cIntroChkSub(GObj *volatile self)
{
    while (stage_CheckAnimationFinish(PAL_JPNKORCN(352, 348)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

void actSt04lDoorEvent(int x)
{
    volatile int local = x;
}
