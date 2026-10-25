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

static ActMail doorDown_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail doorDown_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail doorDown_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail doorDown_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define doorDown_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(doorDown_mes_pal, doorDown_mes_ntsc, doorDown_mes_aug6, doorDown_mes_jun26)

static ActMail ene_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail ene_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail ene_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail ene_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define ene_mes PAL_NTSC_AUG6_JUN26(ene_mes_pal, ene_mes_ntsc, ene_mes_aug6, ene_mes_jun26)

static ActMail st04rDoor_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail st04rDoor_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail st04rDoor_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail st04rDoor_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define st04rDoor_mes                                                                              \
    PAL_NTSC_AUG6_JUN26(st04rDoor_mes_pal, st04rDoor_mes_ntsc, st04rDoor_mes_aug6,                 \
                        st04rDoor_mes_jun26)

static ActMail st04rDoor2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail st04rDoor2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail st04rDoor2_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail st04rDoor2_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define st04rDoor2_mes                                                                             \
    PAL_NTSC_AUG6_JUN26(st04rDoor2_mes_pal, st04rDoor2_mes_ntsc, st04rDoor2_mes_aug6,              \
                        st04rDoor2_mes_jun26)

static ActMail crestHint_mes[2] = {{430}, {429}}; /* derived name */

static void actSt05cDoorDownChk_jun26(GObj *volatile self)
{
    StVec pos;

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    _ACTWait(30);
    actCreateSubThread(actSt05cDoorDownEffect, 21);
    stage_SetAnimation(281, 1, 0);

    pos = doorDownChkPos;
    _ACTWait(30);
    soundSeDefPlay(1171, 0, pos.f, 1);
    _ACTWait(30);
    soundSeDefPlay(1172, 0, pos.f, 1);

    while (stage_CheckAnimationFinish(281) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    gflagOn(141);
    scpBoyControlReadDisable = 0;
}

void actSt05cDoorDownChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt05cDoorDownChk_jun26(self);
        return;
    }
    StVec pos;

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    _ACTWait(30);
    gflagOff(PAL_NTSC_AUG6JUN26(390, 357, 356));
    actCreateSubThread(actSt05cDoorDownEffect, 21);
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(347, 343, 314, 313), 1, 0);

    pos = doorDownChkPos;
    _ACTWait(30);
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1221, 1197), 0, pos.f, 1);
    _ACTWait(30);
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1222, 1198), 0, pos.f, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(347, 343, 314, 313)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    gflagOn(PAL_NTSCAUG6JUN26(165, 148));
    scpBoyControlReadDisable = 0;
}

static void actSt04rDoorChk_ntsc(GObj *volatile self)
{
    GObj *x = self;
    int se;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(230) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1280))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(301, 1, 0);
            while (stage_CheckAnimationFinish(301) == 0) {
                _ACTWait(PAL_NTSC(1, 90));
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1280)->active = 0;
            lt_switch_layout(51);
            scpBoyControlReadDisable = 1;
            scpSleepEnemyAll();
            gflagOn(230);
            stage_SetAnimation(299, 1, 0);
            se = soundSeDefPlay(1306, 0, 0, 1);
            _ACTWait(90);
            soundSeDefStop(se);
            soundSeDefPlay(1307, 0, 0, 1);
            scpWakeupEnemyAll();
            scpBoyControlReadDisable = 0;
            lt_switch_layout(50);
            break;
        }
    }
}

static void actSt04rDoorChk_jun26(GObj *volatile self)
{
    GObj *x = self;
    int se;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(221) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1216))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1216)->active = 0;
            lt_switch_layout(51);
            scpBoyControlReadDisable = 1;
            scpSleepEnemyAll();
            gflagOn(221);
            stage_SetAnimation(268, 1, 0);
            se = soundSeDefPlay(1275, 0, 0, 1);
            _ACTWait(180);
            soundSeDefStop(se);
            soundSeDefPlay(1276, 0, 0, 1);
            scpWakeupEnemyAll();
            scpBoyControlReadDisable = 0;
            lt_switch_layout(50);
            break;
        }
    }
}

static void actSt04rDoorChk_aug6(GObj *volatile self)
{
    GObj *x = self;
    int se;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(230) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1276))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(300, 1, 0);
            while (stage_CheckAnimationFinish(300) == 0) {
                _ACTWait(PAL_AUG6(1, 90));
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1276)->active = 0;
            lt_switch_layout(51);
            scpBoyControlReadDisable = 1;
            scpSleepEnemyAll();
            gflagOn(230);
            stage_SetAnimation(298, 1, 0);
            se = soundSeDefPlay(1306, 0, 0, 1);
            _ACTWait(90);
            soundSeDefStop(se);
            soundSeDefPlay(1307, 0, 0, 1);
            scpWakeupEnemyAll();
            scpBoyControlReadDisable = 0;
            lt_switch_layout(50);
            break;
        }
    }
}

void actSt04rDoorChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rDoorChk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt04rDoorChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt04rDoorChk_aug6(self);
        return;
    }
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
            stage_SetAnimation(PAL_JPNKORCN(334, 330), 1, 0);
            while (stage_CheckAnimationFinish(PAL_JPNKORCN(334, 330)) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1380)->active = 0;
            lt_switch_layout(PAL_JPNKORCN(55, 52));
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
                stage_SetAnimation(PAL_JPNKORCN(332, 328), 0, -1);
                scpFadeIn(3.0f);
            }

            soundSeDefPlay(1331, 0, 0, 1);
            scpWakeupEnemyAll();
            scpBoyControlReadDisable = 0;
            lt_switch_layout(PAL_JPNKORCN(54, 51));
            break;
        }
    }
}

static void actSt04rDoor2Chk_ntsc(GObj *volatile self)
{
    GObj *x = self;
    int se;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(231) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1281))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(302, 1, 0);
            while (stage_CheckAnimationFinish(302) == 0) {
                _ACTWait(PAL_NTSC(1, 90));
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1281)->active = 0;
            lt_switch_layout(51);
            scpBoyControlReadDisable = 1;
            scpSleepEnemyAll();
            gflagOn(231);
            stage_SetAnimation(300, 1, 0);
            se = soundSeDefPlay(1306, 0, 0, 1);
            _ACTWait(90);
            soundSeDefStop(se);
            soundSeDefPlay(1307, 0, 0, 1);
            scpWakeupEnemyAll();
            scpBoyControlReadDisable = 0;
            lt_switch_layout(50);
            break;
        }
    }
}

static void actSt04rDoor2Chk_jun26(GObj *volatile self)
{
    GObj *x = self;
    int se;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(222) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1217))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1217)->active = 0;
            lt_switch_layout(51);
            scpBoyControlReadDisable = 1;
            scpSleepEnemyAll();
            gflagOn(222);
            stage_SetAnimation(269, 1, 0);
            se = soundSeDefPlay(1275, 0, 0, 1);
            _ACTWait(180);
            soundSeDefStop(se);
            soundSeDefPlay(1276, 0, 0, 1);
            scpWakeupEnemyAll();
            scpBoyControlReadDisable = 0;
            lt_switch_layout(50);
            break;
        }
    }
}

static void actSt04rDoor2Chk_aug6(GObj *volatile self)
{
    GObj *x = self;
    int se;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(231) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1277))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(301, 1, 0);
            while (stage_CheckAnimationFinish(301) == 0) {
                _ACTWait(PAL_AUG6(1, 90));
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1277)->active = 0;
            lt_switch_layout(51);
            scpBoyControlReadDisable = 1;
            scpSleepEnemyAll();
            gflagOn(231);
            stage_SetAnimation(299, 1, 0);
            se = soundSeDefPlay(1306, 0, 0, 1);
            _ACTWait(90);
            soundSeDefStop(se);
            soundSeDefPlay(1307, 0, 0, 1);
            scpWakeupEnemyAll();
            scpBoyControlReadDisable = 0;
            lt_switch_layout(50);
            break;
        }
    }
}

void actSt04rDoor2Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt04rDoor2Chk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt04rDoor2Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt04rDoor2Chk_aug6(self);
        return;
    }
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
            stage_SetAnimation(PAL_JPNKORCN(335, 331), 1, 0);
            while (stage_CheckAnimationFinish(PAL_JPNKORCN(335, 331)) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1381)->active = 0;
            lt_switch_layout(PAL_JPNKORCN(55, 52));
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
                stage_SetAnimation(PAL_JPNKORCN(333, 329), 0, -1);
                scpFadeIn(3.0f);
            }

            soundSeDefPlay(1331, 0, 0, 1);
            scpWakeupEnemyAll();
            scpBoyControlReadDisable = 0;
            lt_switch_layout(PAL_JPNKORCN(54, 51));
            break;
        }
    }
}

void actSt05cSolarXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(139, 125, 118)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(304, 300, 271, 270, 244), -1, -2);
    }
}

void actSt05cWaterXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(231, 206, 197)) != 0) {
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1376, 1276, 1272, 1213))->active = 0;
    }
}

void actSt04rDoor(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(255, 230, 221)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(332, 328, 299, 298, 268), 0, 0);
        st04rDoor_mes[0].func = actSt04rDoorChk;
        act->mail = st04rDoor_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(332, 328, 299, 298, 268), 0, -1);
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1380, 1280, 1276, 1216))->active = 0;
    }
}

void actSt04rDoor2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(256, 231, 222)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(333, 329, 300, 299, 269), 0, 0);
        st04rDoor2_mes[0].func = actSt04rDoor2Chk;
        act->mail = st04rDoor2_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(333, 329, 300, 299, 269), 0, -1);
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1381, 1281, 1277, 1217))->active = 0;
    }
}

void actSt05cDoorDown(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    if (gflagChk(PAL_NTSCAUG6_JUN26(165, 148, 141)) == 0) {
        doorDown_mes[0].func = actSt05cDoorDownChk;
        act->mail = doorDown_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }
}

void actSt05cEne(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(167, 150, 143)) == 0) {
        ene_mes[0].func = actSt05cEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }
}

void actSt05cEnemy1(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6_JUN26(168, 151, 144)) == 0) {
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

    while (gflagChk(PAL_NTSCAUG6_JUN26(168, 151, 144)) == 0) {
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

    while (girlGObj == 0 || gflagChk(PAL_NTSCAUG6_JUN26(255, 230, 221)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(600);
    gflagOn(PAL_NTSCAUG6_JUN26(167, 150, 143));
    gflagOn(PAL_NTSCAUG6_JUN26(168, 151, 144));
}

static void actSt04rDoorSub(GObj *volatile self)
{
    int h;

    stage_SetAnimation(PAL_JPNKORCN(332, 328), 1, 0);
    h = soundSeDefPlay(1330, 0, 0, 1);
    _ACTWait(90);
    soundSeDefStop(h);
    demoEnd = 1;
    _ACTWait(0);
}

static void actSt04rDoor2Sub(GObj *volatile self)
{
    int h;

    stage_SetAnimation(PAL_JPNKORCN(333, 329), 1, 0);
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
