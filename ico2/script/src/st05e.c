#include "st05e.h"
#include "layout_texture.h"
#include "thread.h"
#include "act.h"
#include "gobj_process.h"
#include "commonact.h"
#include "way_llf.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "rotObject.h"
#include "typedef.h"
#include "stageSEProc.h"
#include "script.h"
#include "main.h"

static void actSt05eWaterStopSub(GObj *volatile self);

/* .data: four 0x20-byte actor mail packets, one per thread hand-off. */

static ActMail waterMain_mes_pal[2] = {{406, actSt05eWaterSwitch}, {429}}; /* derived name */

static ActMail waterMain_mes_ntsc[2] = {{376, actSt05eWaterSwitch}, {396}}; /* derived name */

static ActMail waterMain_mes_aug6[2] = {{372, actSt05eWaterSwitch}, {392}}; /* derived name */

static ActMail waterMain_mes_jun26[2] = {{356, actSt05eWaterSwitch}, {376}}; /* derived name */

#define waterMain_mes                                                                              \
    PAL_NTSC_AUG6_JUN26(waterMain_mes_pal, waterMain_mes_ntsc, waterMain_mes_aug6,                 \
                        waterMain_mes_jun26)

static ActMail water_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail water_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail water_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail water_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define water_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(water_mes_pal, water_mes_ntsc, water_mes_aug6, water_mes_jun26)

static ActMail waterSwitch_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail waterSwitch_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail waterSwitch_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail waterSwitch_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define waterSwitch_mes                                                                            \
    PAL_NTSC_AUG6_JUN26(waterSwitch_mes_pal, waterSwitch_mes_ntsc, waterSwitch_mes_aug6,           \
                        waterSwitch_mes_jun26)

static ActMail solar_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail solar_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail solar_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail solar_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define solar_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(solar_mes_pal, solar_mes_ntsc, solar_mes_aug6, solar_mes_jun26)

/* .sbss: the demo's own end flag, raised by the subthread the wait loop below
   spins for, and its complement, true when the player skipped the demo with
   START. */
static int demoEnd;

static int demoSkipped; /* derived name */

static void actSt05eWaterStop_ntsc(GObj *volatile self)
{
    lt_switch_layout(51);

    actCreateSubThread(actSt05eWaterFlagOn, 21);

    scpSleepEnemyAll();

    _ACTWait(60);

    stage_SetAnimation(236, 1, 0);

    stage_SetAnimation(235, -1, -2);

    while (stage_CheckAnimationFinish(236) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpSearchGobj(1451)->active = 1;
    scpSearchGobj(1450)->active = 0;

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);

    scpWakeupEnemyAll();

    SetWayGroupActive(5, 1);
}

static void actSt05eWaterStop_aug6jun26(GObj *volatile self)
{
    lt_switch_layout(51);

    if (ICO_JUN26) {
        gflagOn(197);
    } else {
        actCreateSubThread(actSt05eWaterFlagOn, 21);

        scpSleepEnemyAll();
    }

    _ACTWait(60);

    stage_SetAnimation(PAL_JUN26(235, 210), 1, 0);

    stage_SetAnimation(PAL_JUN26(234, 209), -1, -2);

    while (stage_CheckAnimationFinish(PAL_JUN26(235, 210)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpSearchGobj(PAL_JUN26(1445, 1379))->active = 1;
    scpSearchGobj(PAL_JUN26(1444, 1378))->active = 0;

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);

    if (!ICO_JUN26) {
        scpWakeupEnemyAll();
    }

    SetWayGroupActive(5, 1);
}

void actSt05eWaterStop(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt05eWaterStop_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt05eWaterStop_aug6jun26(self);
        return;
    }
    GProc *th;

    lt_switch_layout(PAL_JPNKORCN(55, 52));

    demoEnd = 0;
    demoSkipped = 0;
    actCreateSubThread(actSt05eWaterFlagOn, 21);

    scpSleepEnemyAll();

    th = actCreateSubThread(actSt05eWaterStopSub, 21);
    while (demoEnd == 0 && (!(pad[0].flags & 0x800) || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }
    demoSkipped = demoEnd ^ 1;
    iosThreadSetPri(&th->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }
        stage_SetAnimation(PAL_JPNKORCN(267, 263), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(266, 262), -1, -2);
        scpFadeIn(3.0f);
    }

    scpSearchGobj(1555)->active = 1;
    scpSearchGobj(1554)->active = 0;

    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));

    scpWakeupEnemyAll();

    SetWayGroupActive(5, 1);
}

/* .sdata: the solar stream handle. */
struct SqEntry *solar = 0;

static void actSt05eSolarChk_ntsc(GObj *volatile self)
{
    while (scpIsRotObjectZPlusDirInclude(1452, 265, 275) == 0) {
        _ACTWait(1);
    }

    scpSearchGobj(1452)->active = 0;
    scpSearchGobj(1453)->active = 1;

    if (gflagChk(218) == 0 || gflagChk(219) == 0 || gflagChk(220) == 0) {
        lt_switch_layout(51);
        scpBoyControlReadDisable = 1;

        scpSleepEnemyAll();

        scpAdpcmPlayRequestFunc(52, &solar, 1, 1, 1);

        stage_SetAnimation(237, 1, 0);

        while (stage_CheckAnimationFinish(237) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        if (solar != 0) {
            scpAdpcmFadeClose(solar, 80);
        }

        lt_switch_layout(50);

        scpBoyControlReadDisable = 0;
        scpWakeupEnemyAll();
    }

    gflagOn(207);
    gflagOff(221);
    gflagOff(222);
    gflagOff(224);

    gflagOn(208);
}

static void actSt05eSolarChk_jun26(GObj *volatile self)
{
    while (scpIsRotObjectZPlusDirInclude(1380, 265, 275) == 0) {
        _ACTWait(1);
    }

    scpSearchGobj(1380)->active = 0;
    scpSearchGobj(1381)->active = 1;

    if (gflagChk(209) == 0 || gflagChk(210) == 0 || gflagChk(211) == 0) {
        lt_switch_layout(51);
        scpBoyControlReadDisable = 1;

        scpAdpcmPlayRequestFunc(2, &solar, 1, 1, 1);

        stage_SetAnimation(211, 1, 0);

        while (stage_CheckAnimationFinish(211) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        if (solar != 0) {
            scpAdpcmFadeClose(solar, 80);
        }

        lt_switch_layout(50);

        scpBoyControlReadDisable = 0;
    }

    gflagOn(198);
    gflagOff(212);
    gflagOff(213);
    gflagOff(215);

    gflagOn(199);
}

static void actSt05eSolarChk_aug6(GObj *volatile self)
{
    while (scpIsRotObjectZPlusDirInclude(1446, 265, 275) == 0) {
        _ACTWait(1);
    }

    scpSearchGobj(1446)->active = 0;
    scpSearchGobj(1447)->active = 1;

    if (gflagChk(218) == 0 || gflagChk(219) == 0 || gflagChk(220) == 0) {
        lt_switch_layout(51);
        scpBoyControlReadDisable = 1;

        scpSleepEnemyAll();

        scpAdpcmPlayRequestFunc(52, &solar, 1, 1, 1);

        stage_SetAnimation(236, 1, 0);

        while (stage_CheckAnimationFinish(236) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        if (solar != 0) {
            scpAdpcmFadeClose(solar, 80);
        }

        lt_switch_layout(50);

        scpBoyControlReadDisable = 0;
        scpWakeupEnemyAll();
    }

    gflagOn(207);
    gflagOff(221);
    gflagOff(222);
    gflagOff(224);

    gflagOn(208);
}

void actSt05eSolarChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt05eSolarChk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt05eSolarChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt05eSolarChk_aug6(self);
        return;
    }
    while (scpIsRotObjectZPlusDirInclude(1556, 269, 271) == 0) {
        _ACTWait(1);
    }

    SetRotObjectLockFlag(scpSearchGobj(1556), 1);

    FinishHint(23);
    FinishHint(25);
    FinishHint(26);

    if (gflagChk(243) == 0 || gflagChk(244) == 0 || gflagChk(245) == 0) {
        lt_switch_layout(PAL_JPNKORCN(55, 52));
        scpBoyControlReadDisable = 1;

        scpSleepEnemyAll();

        scpAdpcmPlayRequestFunc(54, &solar, 1, 1, 1);

        stage_SetAnimation(PAL_JPNKORCN(268, 264), 1, 0);

        while (stage_CheckAnimationFinish(PAL_JPNKORCN(268, 264)) == 0) {
            if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
                scpFadeOut(16.0f, 0, 0, 0);
                while (scpFadeChk() != 0) {
                    _ACTWait(1);
                }
                while (lt_fade_status() != 2) {
                    _ACTWait(1);
                }
                stage_SetAnimation(PAL_JPNKORCN(268, 264), 0, -1);
                scpFadeIn(3.0f);
                break;
            }
            _ACTWait(1);
        }

        if (solar != 0) {
            scpAdpcmFadeCloseFunc(&solar, 80);
        }

        lt_switch_layout(PAL_JPNKORCN(54, 51));

        scpBoyControlReadDisable = 0;
        scpWakeupEnemyAll();
    }

    gflagOff(246);
    gflagOff(247);
    gflagOff(248);
    gflagOff(249);
    gflagOn(233);

    gflagOn(232);
}

void actSt05eWater(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(231, 206, 197)) == 0) {
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1555, 1451, 1445, 1379))->active = 0;

        water_mes[0].func = actSt05eWaterMain;
        act->mail = water_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(1554, 1450, 1444, 1378))->active = 0;

        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(266, 262, 235, 234, 209), -1, -2);
    }
}

static void actSt05eSolar_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(207) == 0) {
        scpSearchGobj(1453)->active = 0;

        solar_mes[0].func = actSt05eSolarChk;
        act->mail = solar_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        scpSearchGobj(1452)->active = 0;
    }
}

static void actSt05eSolar_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_JUN26(207, 198)) == 0) {
        scpSearchGobj(PAL_JUN26(1447, 1381))->active = 0;

        solar_mes[0].func = actSt05eSolarChk;
        act->mail = solar_mes;
        ACTSendMailCorrect(self, PAL_JUN26(393, 377));
        _ACTWait(0);
    } else {
        scpSearchGobj(PAL_JUN26(1446, 1380))->active = 0;
    }
}

void actSt05eSolar(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt05eSolar_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt05eSolar_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    SetRotObjectArmRadius(scpSearchGobj(1556), 200.0f);

    if (gflagChk(232) == 0) {
        solar_mes[0].func = actSt05eSolarChk;
        act->mail = solar_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        SetRotObjectLockFlag(scpSearchGobj(1556), 1);

        FinishHint(23);
        FinishHint(25);
        FinishHint(26);
    }
}

static void actSt05eWaterMain_jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    scpBoyControlReadDisable = 0;

    sub->mainMail = waterMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt05eWaterMain(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt05eWaterMain_jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = waterMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt05eWaterSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;
    waterSwitch_mes[0].func = actSt05eWaterStop;
    sub->mail = waterSwitch_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

static void actSt05eWaterFlagOn_ntscaug6jun26(GObj *volatile self)
{
    _ACTWait(
        ico_d2i(ico_dmul(ico_i2d((60 - systemStatus[0] * 10) / systemStatus[1]), ICO_D(11.0))));
    gflagOn(206);
}

void actSt05eWaterFlagOn(GObj *volatile self)
{
    if (ICO_NTSCAUG6JUN26) {
        actSt05eWaterFlagOn_ntscaug6jun26(self);
        return;
    }
    int i = ico_d2i(ico_dmul(ico_i2d((60 - systemStatus[0] * 10) / systemStatus[1]), ICO_D(6.0)));

    riverFadeSpeed = 0.005f;

    while (i-- > 0) {
        if (demoSkipped != 0) {
            riverFadeSpeed = 1000.0f;
            break;
        }
        _ACTWait(1);
    }
    gflagOn(231);
}

static void actSt05eWaterStopSub(GObj *volatile self)
{
    _ACTWait(60);

    stage_SetAnimation(PAL_JPNKORCN(267, 263), 1, 0);

    stage_SetAnimation(PAL_JPNKORCN(266, 262), -1, -2);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(267, 263)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}
