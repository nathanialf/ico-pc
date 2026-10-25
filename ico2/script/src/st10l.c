#include "st10l.h"
#include "layout_texture.h"
#include "pad.h"
#include "s_init.h"
#include "act.h"
#include "commonact.h"
#include "girl_act.h"
#include "way_llf.h"
#include "camera-root.h"
#include "generator.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "typedef.h"
#include "layout_action.h"
#include "script.h"
#include "main.h"

static void actSt10lEneKillChk(GObj *volatile self);

/* .rodata: the girl's way-point packet for actSt10lEneCam3Chk, a 16-byte
   constant vector template whose long long view the copy reads */

static const ConstVec eneCam3ChkPos = {{-33.0f, -72.0f, 470.0f, 0.0f}}; /* derived name */

/* .data: actor mail packets. */

static ActMail floorMain_mes_pal[2] = {{406, actSt10lFloorSwitch}, {429}}; /* derived name */

static ActMail floorMain_mes_ntsc[2] = {{376, actSt10lFloorSwitch}, {396}}; /* derived name */

static ActMail floorMain_mes_aug6[2] = {{372, actSt10lFloorSwitch}, {392}}; /* derived name */

static ActMail floorMain_mes_jun26[2] = {{356, actSt10lFloorSwitch}, {376}}; /* derived name */

#define floorMain_mes                                                                              \
    PAL_NTSC_AUG6_JUN26(floorMain_mes_pal, floorMain_mes_ntsc, floorMain_mes_aug6,                 \
                        floorMain_mes_jun26)

static ActMail floor_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail floor_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail floor_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail floor_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define floor_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(floor_mes_pal, floor_mes_ntsc, floor_mes_aug6, floor_mes_jun26)

static ActMail floorSwitchRight_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail floorSwitchRight_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail floorSwitchRight_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail floorSwitchRight_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define floorSwitchRight_mes                                                                       \
    PAL_NTSC_AUG6_JUN26(floorSwitchRight_mes_pal, floorSwitchRight_mes_ntsc,                       \
                        floorSwitchRight_mes_aug6, floorSwitchRight_mes_jun26)

static ActMail floorSwitchLeft_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail floorSwitchLeft_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail floorSwitchLeft_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail floorSwitchLeft_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define floorSwitchLeft_mes                                                                        \
    PAL_NTSC_AUG6_JUN26(floorSwitchLeft_mes_pal, floorSwitchLeft_mes_ntsc,                         \
                        floorSwitchLeft_mes_aug6, floorSwitchLeft_mes_jun26)

static ActMail floorLeft_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail floorLeft_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail floorLeft_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail floorLeft_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define floorLeft_mes                                                                              \
    PAL_NTSC_AUG6_JUN26(floorLeft_mes_pal, floorLeft_mes_ntsc, floorLeft_mes_aug6,                 \
                        floorLeft_mes_jun26)

static ActMail floorRight_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail floorRight_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail floorRight_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail floorRight_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define floorRight_mes                                                                             \
    PAL_NTSC_AUG6_JUN26(floorRight_mes_pal, floorRight_mes_ntsc, floorRight_mes_aug6,              \
                        floorRight_mes_jun26)

static ActMail gondolaMain_mes_pal[2] = {{407, actSt10lGondolaSwitch}, {429}}; /* derived name */

static ActMail gondolaMain_mes_ntsc[2] = {{377, actSt10lGondolaSwitch}, {396}}; /* derived name */

static ActMail gondolaMain_mes_aug6[2] = {{373, actSt10lGondolaSwitch}, {392}}; /* derived name */

static ActMail gondolaMain_mes_jun26[2] = {{357, actSt10lGondolaSwitch}, {376}}; /* derived name */

#define gondolaMain_mes                                                                            \
    PAL_NTSC_AUG6_JUN26(gondolaMain_mes_pal, gondolaMain_mes_ntsc, gondolaMain_mes_aug6,           \
                        gondolaMain_mes_jun26)

static ActMail gondola_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gondola_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gondola_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail gondola_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define gondola_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(gondola_mes_pal, gondola_mes_ntsc, gondola_mes_aug6, gondola_mes_jun26)

static ActMail gondolaSwitchDown_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gondolaSwitchDown_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gondolaSwitchDown_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail gondolaSwitchDown_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define gondolaSwitchDown_mes                                                                      \
    PAL_NTSC_AUG6_JUN26(gondolaSwitchDown_mes_pal, gondolaSwitchDown_mes_ntsc,                     \
                        gondolaSwitchDown_mes_aug6, gondolaSwitchDown_mes_jun26)

static ActMail gondolaSwitchUp_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gondolaSwitchUp_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gondolaSwitchUp_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail gondolaSwitchUp_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define gondolaSwitchUp_mes                                                                        \
    PAL_NTSC_AUG6_JUN26(gondolaSwitchUp_mes_pal, gondolaSwitchUp_mes_ntsc,                         \
                        gondolaSwitchUp_mes_aug6, gondolaSwitchUp_mes_jun26)

static ActMail gondolaUp_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gondolaUp_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gondolaUp_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail gondolaUp_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define gondolaUp_mes                                                                              \
    PAL_NTSC_AUG6_JUN26(gondolaUp_mes_pal, gondolaUp_mes_ntsc, gondolaUp_mes_aug6,                 \
                        gondolaUp_mes_jun26)

static ActMail gondolaDown_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gondolaDown_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gondolaDown_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail gondolaDown_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define gondolaDown_mes                                                                            \
    PAL_NTSC_AUG6_JUN26(gondolaDown_mes_pal, gondolaDown_mes_ntsc, gondolaDown_mes_aug6,           \
                        gondolaDown_mes_jun26)

static ActMail eneCam1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail eneCam1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail eneCam1_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define eneCam1_mes PAL_NTSC_AUG6JUN26(eneCam1_mes_pal, eneCam1_mes_ntsc, eneCam1_mes_aug6jun26)

static ActMail box_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail box_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail box_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail box_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define box_mes PAL_NTSC_AUG6_JUN26(box_mes_pal, box_mes_ntsc, box_mes_aug6, box_mes_jun26)

static ActMail eneCam2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail eneCam2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail eneCam2_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail eneCam2_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define eneCam2_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(eneCam2_mes_pal, eneCam2_mes_ntsc, eneCam2_mes_aug6, eneCam2_mes_jun26)

static ActMail eneCam3_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail eneCam3_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail eneCam3_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail eneCam3_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define eneCam3_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(eneCam3_mes_pal, eneCam3_mes_ntsc, eneCam3_mes_aug6, eneCam3_mes_jun26)

static ActMail boxA_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail boxA_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail boxA_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail boxA_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define boxA_mes PAL_NTSC_AUG6_JUN26(boxA_mes_pal, boxA_mes_ntsc, boxA_mes_aug6, boxA_mes_jun26)

static ActMail boxB_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail boxB_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail boxB_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail boxB_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define boxB_mes PAL_NTSC_AUG6_JUN26(boxB_mes_pal, boxB_mes_ntsc, boxB_mes_aug6, boxB_mes_jun26)

static ActMail chainMain_mes_pal[2] = {{408, actSt10lChainSwitch}, {429}}; /* derived name */

static ActMail chainMain_mes_ntsc[2] = {{378, actSt10lChainSwitch}, {396}}; /* derived name */

static ActMail chainMain_mes_aug6[2] = {{374, actSt10lChainSwitch}, {392}}; /* derived name */

static ActMail chainMain_mes_jun26[2] = {{358, actSt10lChainSwitch}, {376}}; /* derived name */

#define chainMain_mes                                                                              \
    PAL_NTSC_AUG6_JUN26(chainMain_mes_pal, chainMain_mes_ntsc, chainMain_mes_aug6,                 \
                        chainMain_mes_jun26)

static ActMail chain_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail chain_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail chain_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail chain_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define chain_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(chain_mes_pal, chain_mes_ntsc, chain_mes_aug6, chain_mes_jun26)

static ActMail chainSwitch_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail chainSwitch_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail chainSwitch_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail chainSwitch_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define chainSwitch_mes                                                                            \
    PAL_NTSC_AUG6_JUN26(chainSwitch_mes_pal, chainSwitch_mes_ntsc, chainSwitch_mes_aug6,           \
                        chainSwitch_mes_jun26)

static ActMail eneKill_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail eneKill_mes_ntsc[2] = {{397}, {396}}; /* derived name */

#define eneKill_mes PAL_NTSC(eneKill_mes_pal, eneKill_mes_ntsc)

/* .sdata: the floor, gondola and chain stream handles. */
SqEntry *floor10l = 0;

SqEntry *st10l_gondola_up = 0;

SqEntry *st10l_gondola_down = 0;

SqEntry *chain10l = 0;

static void actSt10lInit_jun26(void)
{
    if (gflagChk(254) != 0) {
        stage_SetAnimation(310, 0, 89);
    } else {
        stage_SetAnimation(310, 0, 0);
    }
}

void actSt10lInit(void)
{
    if (ICO_JUN26) {
        actSt10lInit_jun26();
        return;
    }
    if (gflagChk(PAL_NTSCAUG6JUN26(289, 263)) != 0) {
        SetWayGroupActive(22, 1);
        SetWayGroupActive(23, 1);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(379, 375, 344, 343), 0, 89);
    } else {
        SetWayGroupActive(20, 1);
        SetWayGroupActive(21, 1);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(379, 375, 344, 343), 0, 0);
    }
}

static void actSt10lFloorLeft_jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    if (gflagChk(255) == 0 && girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        gflagOn(255);
    }

    scpAdpcmPlayRequestFunc(84, 0, 1, 1, 1);

    stage_SetAnimation(310, 1, 0);

    gflagOn(254);

    while (stage_CheckAnimationFrame(310, 89, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (gflagChk(255) != 0) {
        gflagOn(260);
    }

    _ACTWait(60);
    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 9);

    floorLeft_mes[0].func = actSt10lFloorMain;
    sub->mail = floorLeft_mes;
    ACTSendMailCorrect(self, 377);
    _ACTWait(0);
}

void actSt10lFloorLeft(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt10lFloorLeft_jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    if (gflagChk(PAL_NTSCAUG6JUN26(290, 264)) == 0 && girlGObj != 0 &&
        scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        gflagOn(PAL_NTSCAUG6JUN26(290, 264));
    }

    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x2000000) != 0) {
        scpPlayStart(girlGObj);
        scpPlayMot(girlGObj, PAL_NTSCAUG6JUN26(532, 514));
    }

    if (gflagChk(PAL_NTSCAUG6JUN26(290, 264)) != 0 && girlGObj != 0 &&
        scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        scpPlayPosSet(girlGObj, -167.0f, -72.0f, -705.0f);
        scpPlayStart(girlGObj);
        scpPlayMot(girlGObj, PAL_NTSCAUG6JUN26(532, 514));
    }

    scpAdpcmPlayRequestFunc(PAL_NTSCAUG6JUN26(91, 89), &floor10l, 1, 1, 1);

    while (floor10l == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(379, 375, 344, 343), 1, 0);

    SetWayGroupActive(20, 0);
    SetWayGroupActive(21, 0);
    SetWayGroupActive(22, 1);
    SetWayGroupActive(23, 1);

    gflagOn(PAL_NTSCAUG6JUN26(289, 263));

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6JUN26(379, 375, 344, 343), 89, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(290, 264)) != 0) {
        gflagOn(PAL_NTSCAUG6JUN26(296, 270));
    }

    if (ICO_NTSC) {
        _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 10);

        if (gflagChk(269) == 0) {
            scpBoyControlReadDisable = 0;
            lt_switch_layout(50);
            scpWakeupEnemyAll();

            if (girlGObj != 0) {
                scpPlayEnd(girlGObj);
            }
        }
    } else if (ICO_AUG6JUN26) {
        _ACTWait(60);
        _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 9);
    } else {
        _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 10);

        if (gflagChk(295) == 0) {
            scpBoyControlReadDisable = 0;
            lt_switch_layout(PAL_JPNKORCN(54, 51));
            scpWakeupEnemyAll();

            if (girlGObj != 0) {
                scpPlayEnd(girlGObj);
            }
        }
    }

    floorLeft_mes[0].func = actSt10lFloorMain;
    sub->mail = floorLeft_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

static void actSt10lFloorRight_jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpAdpcmPlayRequestFunc(84, 0, 1, 1, 1);

    stage_SetAnimation(310, 1, 90);

    gflagOff(254);

    while (stage_CheckAnimationFrame(310, 180, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 11);

    floorRight_mes[0].func = actSt10lFloorMain;
    sub->mail = floorRight_mes;
    ACTSendMailCorrect(self, 377);
    _ACTWait(0);
}

void actSt10lFloorRight(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt10lFloorRight_jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    if (gflagChk(PAL_NTSCAUG6JUN26(290, 264)) != 0 && girlGObj != 0 &&
        scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        scpPlayPosSet(girlGObj, -196.0f, -72.0f, 62.0f);
        scpPlayStart(girlGObj);
        scpPlayMot(girlGObj, PAL_NTSCAUG6JUN26(532, 514));
    }

    scpAdpcmPlayRequestFunc(PAL_NTSCAUG6JUN26(91, 89), &floor10l, 1, 1, 1);

    while (floor10l == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(379, 375, 344, 343), 1, 90);

    SetWayGroupActive(20, 1);
    SetWayGroupActive(21, 1);
    SetWayGroupActive(22, 0);
    SetWayGroupActive(23, 0);

    gflagOff(PAL_NTSCAUG6JUN26(289, 263));

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6JUN26(379, 375, 344, 343), 180, 1) ==
           0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 11);

    if (ICO_NTSC) {
        scpBoyControlReadDisable = 0;
        lt_switch_layout(50);
        scpWakeupEnemyAll();

        if (girlGObj != 0) {
            scpPlayEnd(girlGObj);
        }
    } else if (ICO_AUG6JUN26) {
    } else {
        scpBoyControlReadDisable = 0;
        lt_switch_layout(PAL_JPNKORCN(54, 51));
        scpWakeupEnemyAll();

        if (girlGObj != 0) {
            scpPlayEnd(girlGObj);
        }
    }

    floorRight_mes[0].func = actSt10lFloorMain;
    sub->mail = floorRight_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

static void actSt10lGondolaUp_jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpAdpcmPlayRequestFunc(81, &st10l_gondola_up, 1, 1, 1);

    while (st10l_gondola_up == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(311, 1, 0);

    while (stage_CheckAnimationFrame(311, 169, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFrame(311, 179, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    gflagOn(256);

    if (st10l_gondola_up != 0) {
        scpAdpcmFadeClose(st10l_gondola_up, 256);
    }

    while (scpAdpcmCloseChk(st10l_gondola_up) != 0) {
        _ACTWait(1);
    }

    gondolaUp_mes[0].func = actSt10lGondolaMain;
    sub->mail = gondolaUp_mes;
    ACTSendMailCorrect(self, 377);
    _ACTWait(0);
}

void actSt10lGondolaUp(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt10lGondolaUp_jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    scpAdpcmPlayRequestFunc(PAL_NTSCAUG6JUN26(88, 86), &st10l_gondola_up, 1, 1, 1);

    while (st10l_gondola_up == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(380, 376, 345, 344), 1, 0);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6JUN26(380, 376, 345, 344), 169, 0) ==
           0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6JUN26(380, 376, 345, 344), 179, 1) ==
           0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    gflagOn(PAL_NTSCAUG6JUN26(291, 265));

    if (st10l_gondola_up != 0) {
        if (ICO_NTSCAUG6JUN26) {
            scpAdpcmFadeClose(st10l_gondola_up, 256);
        } else {
            scpAdpcmFadeCloseFunc(&st10l_gondola_up, 256);
        }
    }

    while (PAL_NTSCAUG6JUN26(scpAdpcmCloseChkFunc(&st10l_gondola_up),
                             scpAdpcmCloseChk(st10l_gondola_up)) != 0) {
        _ACTWait(1);
    }

    if (ICO_NTSC) {
        scpBoyControlReadDisable = 0;
        scpWakeupEnemyAll();
        lt_switch_layout(50);
    } else if (ICO_AUG6JUN26) {
    } else {
        scpBoyControlReadDisable = 0;
        scpWakeupEnemyAll();
        lt_switch_layout(PAL_JPNKORCN(54, 51));
    }

    gondolaUp_mes[0].func = actSt10lGondolaMain;
    sub->mail = gondolaUp_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

static void actSt10lGondolaDown_jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    SetGirlDangerGObj(boyGObj);

    scpAdpcmPlayRequestFunc(82, &st10l_gondola_down, 1, 1, 1);

    while (st10l_gondola_down == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(311, 1, 180);

    while (stage_CheckAnimationFrame(311, 340, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 17);

    while (stage_CheckAnimationFrame(311, 360, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    gflagOff(256);

    if (st10l_gondola_down != 0) {
        scpAdpcmFadeClose(st10l_gondola_down, 256);
    }

    while (scpAdpcmCloseChk(st10l_gondola_down) != 0) {
        _ACTWait(1);
    }

    ClearGirlDangerGObj();

    gondolaDown_mes[0].func = actSt10lGondolaMain;
    sub->mail = gondolaDown_mes;
    ACTSendMailCorrect(self, 377);
    _ACTWait(0);
}

void actSt10lGondolaDown(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt10lGondolaDown_jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    SetGirlDangerGObj(boyGObj);

    scpAdpcmPlayRequestFunc(PAL_NTSCAUG6JUN26(89, 87), &st10l_gondola_down, 1, 1, 1);

    while (st10l_gondola_down == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(380, 376, 345, 344), 1, 180);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6JUN26(380, 376, 345, 344), 340, 0) ==
           0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 17);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6JUN26(380, 376, 345, 344), 360, 1) ==
           0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    gflagOff(PAL_NTSCAUG6JUN26(291, 265));

    if (st10l_gondola_down != 0) {
        if (ICO_NTSCAUG6JUN26) {
            scpAdpcmFadeClose(st10l_gondola_down, 256);
        } else {
            scpAdpcmFadeCloseFunc(&st10l_gondola_down, 256);
        }
    }

    while (PAL_NTSCAUG6JUN26(scpAdpcmCloseChkFunc(&st10l_gondola_down),
                             scpAdpcmCloseChk(st10l_gondola_down)) != 0) {
        _ACTWait(1);
    }

    ClearGirlDangerGObj();

    if (ICO_NTSC) {
        scpBoyControlReadDisable = 0;
        scpWakeupEnemyAll();
        lt_switch_layout(50);
    } else if (ICO_AUG6JUN26) {
    } else {
        scpBoyControlReadDisable = 0;
        scpWakeupEnemyAll();
        lt_switch_layout(PAL_JPNKORCN(54, 51));
    }

    gondolaDown_mes[0].func = actSt10lGondolaMain;
    sub->mail = gondolaDown_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

static void actSt10lEneCam2Chk_ntsc(GObj *volatile self)
{
    while (girlGObj == 0 || scpTriggerFloorAttr(boyGObj, 0x4000000) == 0 || gflagChk(261) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    scpSleepEnemyOne(3453);
    scpSleepEnemyOne(903);
    scpSleepEnemyOne(906);
    scpSleepEnemyOne(907);
    scpSleepSpiderGroupOne(908);

    _ACTWait(30);

    gflagOn(268);

    stage_SetAnimation(346, 1, 0);

    while (stage_CheckAnimationFinish(346) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    _ACTWait(60);

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;

    scpWakeupEnemyOne(3453);
    scpWakeupEnemyOne(903);
    scpWakeupEnemyOne(906);
    scpWakeupEnemyOne(907);
    scpWakeupSpiderGroupOne(908);
}

static void actSt10lEneCam2Chk_jun26(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    _ACTWait(30);

    gflagOn(258);

    stage_SetAnimation(312, 1, 0);

    while (stage_CheckAnimationFinish(312) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
}

static void actSt10lEneCam2Chk_aug6(GObj *volatile self)
{
    while (girlGObj == 0 || scpTriggerFloorAttr(boyGObj, 0x4000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    _ACTWait(30);

    gflagOn(268);

    stage_SetAnimation(345, 1, 0);

    while (stage_CheckAnimationFinish(345) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
}

void actSt10lEneCam2Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt10lEneCam2Chk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt10lEneCam2Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt10lEneCam2Chk_aug6(self);
        return;
    }
    int save;

    while (girlGObj == 0 || scpTriggerFloorAttr(boyGObj, 0x4000000) == 0 || gflagChk(287) == 0 ||
           GOBJ_ACT(girlGObj)->actMode == 111) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;

    save = iosPadActRequestEnable;
    iosPadActRequestEnable = 0;

    scpKillEnemyOne(992);
    scpKillEnemyOne(993);
    scpKillSpiderGroup(994);
    scpSleepEnemyOne(PAL_JPNKORCN(3757, 3734));
    scpSleepEnemyOne(991);
    scpSleepEnemyOne(1006);
    scpSleepSpiderGroupOne(1007);

    _ACTWait(30);

    gflagOn(294);

    iosPadActRequestEnable = save;

    stage_SetAnimation(PAL_JPNKORCN(381, 377), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(381, 377)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 3);

    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpBoyControlReadDisable = 0;

    scpWakeupEnemyOne(PAL_JPNKORCN(3757, 3734));
    scpWakeupEnemyOne(992);
    scpWakeupEnemyOne(993);
    scpWakeupSpiderGroupOne(994);
    scpWakeupEnemyOne(991);
    scpWakeupEnemyOne(1006);
    scpWakeupSpiderGroupOne(1007);
}

static void actSt10lEneCam3Chk_jun26(GObj *volatile self)
{
    long long buf[2];

    while (gflagChk(260) == 0) {
        _ACTWait(1);
    }

    gflagOn(259);

    stage_SetAnimation(313, 1, 0);

    buf[0] = eneCam3ChkPos.d[0];
    buf[1] = eneCam3ChkPos.d[1];
    _SCPMoveCharactorByWay(girlGObj, 0, (float *)buf, 100.0f, 0);

    while (stage_CheckAnimationFinish(313) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    gflagOff(259);

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 3);

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
}

void actSt10lEneCam3Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt10lEneCam3Chk_jun26(self);
        return;
    }
    long long buf[2];

    while (gflagChk(PAL_NTSCAUG6JUN26(296, 270)) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSCAUG6JUN26) {
        gflagOn(269);
    } else {
        gflagOn(295);
        FinishHint(14);
    }

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(382, 378, 347, 346), 1, 0);

    buf[0] = eneCam3ChkPos.d[0];
    buf[1] = eneCam3ChkPos.d[1];
    _SCPMoveCharactorByWay(girlGObj, 0, (float *)buf, 100.0f, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(382, 378, 347, 346)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    gflagOff(PAL_NTSCAUG6JUN26(295, 269));

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 3);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
}

static void actSt10lChainMove_ntsc(GObj *volatile self)
{
    lt_switch_layout(51);
    scpSleepEnemyAll();

    gflagOn(261);

    _ACTWait(30);

    scpAdpcmPlayRequestFunc(95, &chain10l, 1, 1, 1);

    while (chain10l == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(349, 1, 0);
    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFrame(349, 240, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpSearchGobj(899)->active = 1;

    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static void actSt10lChainMove_aug6jun26(GObj *volatile self)
{
    lt_switch_layout(51);
    if (!ICO_JUN26) {
        scpSleepEnemyAll();
    }

    gflagOn(PAL_JUN26(261, 252));

    _ACTWait(30);

    if (ICO_JUN26) {
        scpAdpcmPlayRequestFunc(86, 0, 1, 1, 1);
    } else {
        scpAdpcmPlayRequestFunc(95, &chain10l, 1, 1, 1);
    }

    if (ICO_JUN26) {
        stage_SetAnimation(315, 1, 0);
    } else {
        while (chain10l == 0) {
            _ACTWait(1);
        }

        stage_SetAnimation(348, 1, 0);
    }
    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFrame(PAL_JUN26(348, 315), 240, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpSearchGobj(PAL_JUN26(898, 861))->active = 1;

    if (!ICO_JUN26) {
        scpWakeupEnemyAll();
    }
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actSt10lChainMove(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt10lChainMove_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt10lChainMove_aug6jun26(self);
        return;
    }
    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpSleepEnemyAll();

    gflagOn(287);
    FinishHint(13);

    _ACTWait(30);

    scpAdpcmPlayRequestFunc(97, &chain10l, 1, 1, 1);

    while (chain10l == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JPNKORCN(384, 380), 1, 0);
    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(384, 380), 240, 0) == 0) {
        if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
            scpFadeOut(16.0f, 0, 0, 0);
            scpAdpcmFadeCloseFunc(&chain10l, 512);

            while (scpFadeChk() != 0) {
                _ACTWait(1);
            }
            while (lt_fade_status() != 2) {
                _ACTWait(1);
            }

            stage_SetAnimation(PAL_JPNKORCN(384, 380), 1, 239);
            scpFadeIn(3.0f);
            break;
        }
        _ACTWait(1);
    }

    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1]);

    stage_SetAnimation(PAL_JPNKORCN(384, 380), 0, 0);

    scpSearchGobj(985)->active = 1;
}

static void actSt10lChain_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(261) == 0) {
        stage_SetAnimation(349, 0, 0);

        scpSearchGobj(899)->active = 0;

        chain_mes[0].func = actSt10lChainMain;
        act->mail = chain_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        stage_SetAnimation(349, 0, -1);
    }
}

static void actSt10lChain_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_JUN26(261, 252)) == 0) {
        stage_SetAnimation(PAL_JUN26(348, 315), 0, 0);

        scpSearchGobj(PAL_JUN26(898, 861))->active = 0;

        chain_mes[0].func = actSt10lChainMain;
        act->mail = chain_mes;
        ACTSendMailCorrect(self, PAL_JUN26(393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JUN26(348, 315), 0, -1);
    }
}

void actSt10lChain(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt10lChain_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt10lChain_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(287) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(384, 380), 0, 0);

        scpSearchGobj(985)->active = 0;

        chain_mes[0].func = actSt10lChainMain;
        act->mail = chain_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(384, 380), 0, 0);
        FinishHint(13);
    }
}

void actSt10lFloor(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    floor_mes[0].func = actSt10lFloorMain;
    act->mail = floor_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

void actSt10lGondola(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(291, 265, 256)) != 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(380, 376, 345, 344, 311), 0, 0);
        _ACTWait(10);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(380, 376, 345, 344, 311), 0, 179);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(380, 376, 345, 344, 311), 0, 0);
    }

    gondola_mes[0].func = actSt10lGondolaMain;
    act->mail = gondola_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

void actSt10lSekizo(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    scpSekizou(self, PAL_NTSCAUG6_JUN26(288, 262, 253),
               PAL_JPNKORCN_NTSC_AUG6_JUN26(383, 379, 348, 347, 314), 0,
               PAL_NTSCAUG6_JUN26(19, 18, 21), 0.0f, -72.0f, 1274.0f, 76.0f, -72.0f, 1274.0f);
}

void actSt10lBox(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    box_mes[0].func = actSt10lBoxChk;
    act->mail = box_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

static void actSt10lEnemy1_1_jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    Generator_Mask(self);

    while (girlGObj == 0 || scpTriggerFloorAttr(girlGObj, 0x1000000) == 0 || gflagChk(257) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt10lEnemy1_1(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt10lEnemy1_1_jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    Generator_Mask(self);
    if (!ICO_NTSCAUG6JUN26) {
        Generator_Mask(scpSearchGobj(997));
    }

    while (gflagChk(PAL_NTSCAUG6JUN26(293, 267)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(116);

    Generator_Call(self);
    Generator_MaskOff(self);
    if (!ICO_NTSCAUG6JUN26) {
        Generator_Call(scpSearchGobj(997));
    }
}

void actSt10lEnemy1_2(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6JUN26(293, 267)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(100);

    Generator_Call(self);
    Generator_MaskOff(self);
}

static void actSt10lEnemy2_1_jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(258) == 0) {
        _ACTWait(1);
    }
    _ACTWait(116);

    Generator_Call(self);
}

void actSt10lEnemy2_1(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt10lEnemy2_1_jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6JUN26(294, 268)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(116);

    Generator_Call(self);
    Generator_MaskOff(self);
}

static void actSt10lEnemy2_2_jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(258) == 0) {
        _ACTWait(1);
    }
    _ACTWait(100);

    Generator_Call(self);
}

void actSt10lEnemy2_2(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt10lEnemy2_2_jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6JUN26(294, 268)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(100);

    Generator_Call(self);
    Generator_MaskOff(self);
}

static void actSt10lEnemy2_3_jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(258) == 0) {
        _ACTWait(1);
    }
    _ACTWait(130);

    Generator_Call(self);
}

void actSt10lEnemy2_3(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt10lEnemy2_3_jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6JUN26(294, 268)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(130);

    Generator_Call(self);
    Generator_MaskOff(self);
}

static void actSt10lEnemy3_1_jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(259) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    Generator_Call(self);
}

void actSt10lEnemy3_1(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt10lEnemy3_1_jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6JUN26(295, 269)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    Generator_Call(self);
    Generator_MaskOff(self);
}

static void actSt10lEnemy3_2_jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(259) == 0) {
        _ACTWait(1);
    }

    _ACTWait(320);
    Generator_Call(self);
}

void actSt10lEnemy3_2(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt10lEnemy3_2_jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6JUN26(295, 269)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(320);

    Generator_Call(self);
    Generator_MaskOff(self);
}

void actSt10lEneCam1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(293, 267)) == 0) {
        eneCam1_mes[0].func = actSt10lEneCam1Chk;
        act->mail = eneCam1_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt10lEneCam2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(294, 268, 258)) == 0) {
        eneCam2_mes[0].func = actSt10lEneCam2Chk;
        act->mail = eneCam2_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }
}

static void actSt10lEneCam3_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(270) == 0) {
        eneCam3_mes[0].func = actSt10lEneCam3Chk;
        act->mail = eneCam3_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actSt10lEneCam3_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_JUN26(270, 260)) == 0) {
        eneCam3_mes[0].func = actSt10lEneCam3Chk;
        act->mail = eneCam3_mes;
        ACTSendMailCorrect(self, PAL_JUN26(393, 377));
        _ACTWait(0);
    }
}

void actSt10lEneCam3(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt10lEneCam3_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt10lEneCam3_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(296) == 0) {
        eneCam3_mes[0].func = actSt10lEneCam3Chk;
        act->mail = eneCam3_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        FinishHint(14);
    }
}

void actSt10lEneKill(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSC(297, 271)) == 0) {
        eneKill_mes[0].func = actSt10lEneKillChk;
        act->mail = eneKill_mes;
        ACTSendMailCorrect(self, PAL_NTSC(430, 397));
        _ACTWait(0);
    }
}

void actSt10lBoxA(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSC_AUG6_JUN26(298, 272, 271, 261)) == 0) {
        boxA_mes[0].func = actSt10lBoxAChk;
        act->mail = boxA_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(977, 891, 890, 853))->active = 0;
    }
}

void actSt10lBoxB(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSC_AUG6_JUN26(299, 273, 272, 262)) == 0) {
        boxB_mes[0].func = actSt10lBoxBChk;
        act->mail = boxB_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(978, 892, 891, 854))->active = 0;
    }
}

void actSt10lGateXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(174, 155, 148)) == 0) {
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(971, 885, 884, 847))->active = 0;
    } else {
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(970, 884, 883, 846))->active = 0;
    }
}

static void actSt10lFloorMain_jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    if (gflagChk(259) == 0) {
        scpBoyControlReadDisable = 0;
        lt_switch_layout(50);
        scpWakeupEnemyAll();
    }

    sub->mainMail = floorMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt10lFloorMain(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt10lFloorMain_jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);
    if (ICO_AUG6JUN26) {
        if (gflagChk(269) == 0) {
            scpBoyControlReadDisable = 0;
            lt_switch_layout(50);
            scpWakeupEnemyAll();

            if (girlGObj != 0) {
                scpPlayEnd(girlGObj);
            }
        }
    }

    sub->mainMail = floorMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt10lFloorSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    if (gflagChk(PAL_NTSCAUG6_JUN26(289, 263, 254)) != 0) {
        floorSwitchRight_mes[0].func = actSt10lFloorRight;
        sub->mail = floorSwitchRight_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }

    floorSwitchLeft_mes[0].func = actSt10lFloorLeft;
    sub->mail = floorSwitchLeft_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

void actSt10lGondolaMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    if (ICO_AUG6JUN26) {
        scpBoyControlReadDisable = 0;
        scpWakeupEnemyAll();
        lt_switch_layout(50);
    }

    sub->mainMail = gondolaMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt10lGondolaSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    if (gflagChk(PAL_NTSCAUG6_JUN26(291, 265, 256)) != 0) {
        gondolaSwitchDown_mes[0].func = actSt10lGondolaDown;
        sub->mail = gondolaSwitchDown_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }

    gondolaSwitchUp_mes[0].func = actSt10lGondolaUp;
    sub->mail = gondolaSwitchUp_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

void actSt10lEneCam1Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (
        (PAL_NTSCAUG6JUN26(girlGObj == 0 || gflagChk(292) == 0,
                           scpTriggerFloorAttr(girlGObj, 0x1000000) == 0 || gflagChk(266) == 0)) &&
        (scpTriggerFloorAttr(boyGObj, 0x2000000) == 0 ||
         scpTriggerFloorAttr(girlGObj, 0x2000000) == 0)) {
        _ACTWait(1);
    }

    gflagOn(PAL_NTSCAUG6JUN26(293, 267));
}

void actSt10lBoxChk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(scpSearchGobj(PAL_NTSC_AUG6_JUN26(988, 902, 901, 864)), 0x1000000) ==
           0) {
        _ACTWait(1);
    }

    gflagOn(PAL_NTSCAUG6_JUN26(292, 266, 257));
}

void actSt10lSekizoEvent(int x)
{
    volatile int local = x;
}

static void actSt10lBoxAChk_jun26(GObj *volatile self)
{
    while (scpTriggerBall(self, scpSearchGobj(853), 100.0f) == 0) {
        _ACTWait(1);
    }

    gflagOn(261);
    scpSearchGobj(853)->active = 0;
}

void actSt10lBoxAChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt10lBoxAChk_jun26(self);
        return;
    }
    while (scpTriggerBall(self, scpSearchGobj(PAL_NTSC_AUG6JUN26(977, 891, 890)), 100.0f) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSC) {
        gflagOn(272);
        scpSearchGobj(891)->active = 0;
        _ACTWait(30);
        soundSeDefPlay(1246, 0, 0, 1);
    } else if (ICO_AUG6JUN26) {
        gflagOn(271);
        scpSearchGobj(890)->active = 0;
    } else {
        gflagOn(298);
        scpSearchGobj(977)->active = 0;
        _ACTWait(30);
        soundSeDefPlay(1270, 0, 0, 1);
    }
}

static void actSt10lBoxBChk_jun26(GObj *volatile self)
{
    while (scpTriggerBall(self, scpSearchGobj(854), 100.0f) == 0) {
        _ACTWait(1);
    }

    gflagOn(262);
    scpSearchGobj(854)->active = 0;
}

void actSt10lBoxBChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt10lBoxBChk_jun26(self);
        return;
    }
    while (scpTriggerBall(self, scpSearchGobj(PAL_NTSC_AUG6JUN26(978, 892, 891)), 100.0f) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSC) {
        gflagOn(273);
        scpSearchGobj(892)->active = 0;
        _ACTWait(30);
        soundSeDefPlay(1247, 0, 0, 1);
    } else if (ICO_AUG6JUN26) {
        gflagOn(272);
        scpSearchGobj(891)->active = 0;
    } else {
        gflagOn(299);
        scpSearchGobj(978)->active = 0;
        _ACTWait(30);
        soundSeDefPlay(1271, 0, 0, 1);
    }
}

void actSt10lChainMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    if (ICO_AUG6JUN26) {
        scpBoyControlReadDisable = 0;
    }
    sub->mainMail = chainMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt10lChainSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    scpBoyControlReadDisable = 1;

    chainSwitch_mes[0].func = actSt10lChainMove;
    sub->mail = chainSwitch_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

static void actSt10lEneKillChk(GObj *volatile self)
{
    int save;

    while (scpTriggerBall(self, boyGObj, 500.0f) == 0) {
        _ACTWait(1);
    }

    enable_game_pause = 0;
    _ACTWait(1);

    save = iosPadActRequestEnable;
    iosPadActRequestEnable = 0;
    _ACTWait(30);

    if (ICO_NTSC) {
        gflagOn(271);
        scpKillEnemyOne(904);
        scpKillEnemyOne(913);
        scpKillEnemyOne(914);
        scpKillSpiderGroup(915);
    } else {
        gflagOn(297);
        scpKillEnemyOne(1000);
        scpKillEnemyOne(1001);
        scpKillSpiderGroup(1002);
    }
    _ACTWait(30);

    iosPadActRequestEnable = save;
    _ACTWait(1);
    enable_game_pause = 1;
}
