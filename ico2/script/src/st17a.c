#include "st17a.h"
#include "layout_texture.h"
#include "pad.h"
#include "debug.h"
#include "obj_manager.h"
#include "lws_kyomi.h"
#include "s_init.h"
#include "act-game.h"
#include "commonact.h"
#include "way_llf.h"
#include "camera-root.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "act.h"
#include "e3.h"
#include "typedef.h"
#include "script.h"
#include "main.h"

static void actSt17aFallChk(GObj *volatile self);
static void actSt17aHint1Chk(GObj *volatile self);

static ActMail linkTest_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail linkTest_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail linkTest_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail linkTest_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define linkTest_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(linkTest_mes_pal, linkTest_mes_ntsc, linkTest_mes_aug6, linkTest_mes_jun26)

static ActMail doorInit_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail doorInit_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail doorInit_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail doorInit_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define doorInit_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(doorInit_mes_pal, doorInit_mes_ntsc, doorInit_mes_aug6, doorInit_mes_jun26)

static ActMail doorDown_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail doorDown_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail doorDown_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail doorDown_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define doorDown_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(doorDown_mes_pal, doorDown_mes_ntsc, doorDown_mes_aug6, doorDown_mes_jun26)

static ActMail doorUp_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail doorUp_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail doorUp_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail doorUp_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define doorUp_mes                                                                                 \
    PAL_NTSC_AUG6_JUN26(doorUp_mes_pal, doorUp_mes_ntsc, doorUp_mes_aug6, doorUp_mes_jun26)

static ActMail doorUpChk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail doorUpChk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail doorUpChk_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define doorUpChk_mes                                                                              \
    PAL_NTSC_AUG6JUN26(doorUpChk_mes_pal, doorUpChk_mes_ntsc, doorUpChk_mes_aug6jun26)

/* actSt17aDoorDownChk's record */
static ActMail door_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define door_mes PAL_NTSC_AUG6JUN26(door_mes_pal, door_mes_ntsc, door_mes_aug6jun26)

static ActMail hasi_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail hasi_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail hasi_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail hasi_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define hasi_mes PAL_NTSC_AUG6_JUN26(hasi_mes_pal, hasi_mes_ntsc, hasi_mes_aug6, hasi_mes_jun26)

static ActMail intro_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail intro_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail intro_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail intro_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define intro_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(intro_mes_pal, intro_mes_ntsc, intro_mes_aug6, intro_mes_jun26)

static ActMail hint1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail fall_mes[2] = {{430}, {429}}; /* derived name */

static const ConstVec doorChkSePos = {{6646.0f, -2157.0f, 1102.0f, 0.0f}}; /* derived name */

static const ConstVec doorUpEffectPos = {{6690.0f, -2000.0f, 1100.0f, 1.0f}}; /* derived name */

static const ConstVec doorDownEffectPos = {{6690.0f, -2300.0f, 1100.0f, 1.0f}}; /* derived name */

static const ConstVec doorDownEffect2Pos = {{6600.0f, -2000.0f, 1100.0f, 1.0f}}; /* derived name */

static const ConstVec hasiChkSePos = {{3587.0f, -2072.0f, 1124.0f, 0.0f}}; /* derived name */

static const ConstVec hasiChkGirlDir = {{10000.0f, 0.0f, 0.0f, 1.0f}}; /* derived name */

static const ConstVec hasiChkGirlWay_ntsc = {{3830.0f, -2072.0f, 1113.0f, 0.0f}}; /* derived name */

static const ConstVec hasiChkGirlWay_aug6jun26 = {
    {3882.0f, -2072.0f, 1112.0f, 0.0f}}; /* derived name */

#define hasiChkGirlWay (*PAL_AUG6JUN26(&hasiChkGirlWay_ntsc, &hasiChkGirlWay_aug6jun26))

static void actSt17aDoor_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(30) == 0) {
        stage_SetAnimation(100, 0, 0);

        doorInit_mes[0].func = actSt17aDoorUpChk;
        act->mail = doorInit_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    } else if (scpTriggerBall(self, boyGObj, 200.0f) != 0) {
        stage_SetAnimation(100, 0, 0);

        _ACTWait(60);
        doorDown_mes[0].func = actSt17aDoorDownChk;
        act->mail = doorDown_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    } else {
        stage_SetAnimation(99, 0, 0);
        doorUp_mes[0].func = actSt17aDoorUpChk;
        act->mail = doorUp_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    }
}

void actSt17aDoor(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt17aDoor_jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(32, 30)) == 0) {
        stage_SetLoopFlag(PAL_JPNKORCN_NTSC_AUG6JUN26(131, 127, 111, 110), 1);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(131, 127, 111, 110), 1, 0);

        doorInit_mes[0].func = actSt17aDoorUpChk;
        act->mail = doorInit_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else if (scpTriggerBall(self, boyGObj, 200.0f) != 0 ||
               ((void *)girlGObj != 0 && scpTriggerBall(self, (void *)girlGObj, 400.0f) != 0)) {
        stage_SetLoopFlag(PAL_JPNKORCN_NTSC_AUG6JUN26(131, 127, 111, 110), 1);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(131, 127, 111, 110), 1, 0);

        _ACTWait(60);
        doorDown_mes[0].func = actSt17aDoorDownChk;
        act->mail = doorDown_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(129, 125, 109, 108), 0, 0);
        doorUp_mes[0].func = actSt17aDoorUpChk;
        act->mail = doorUp_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

static void actSt17aDoorUpChk_jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    static ActMail doorUpChk_mes_l[2] = {{377}, {376}}; /* derived name */
    long long buf[2];
    int h;

    while (scpTriggerFloorAttrTargetMan(self, 0x3000000) == 0) {
        _ACTWait(1);
    }

    _ACTWait(15);

    actCreateSubThread(actSt17aDoorUpEffect, 21);

    scpWakeupItemWithBoundary(6573.0f, -2077.0f, 1089.0f, 100.0f);

    stage_SetAnimation(99, 1, 0);

    buf[0] = doorChkSePos.d[0];
    buf[1] = doorChkSePos.d[1];
    soundSeDefPlay(1170, 0, (float *)buf, 1);
    _ACTWait(30);

    h = soundSeDefPlay(1171, 0, (float *)buf, 1);
    _ACTWait(60);
    soundSeDefStop(h);

    soundSeDefPlay(1172, 0, (float *)buf, 1);

    while (stage_CheckAnimationFinish(99) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    doorUpChk_mes_l[0].func = actSt17aDoorDownChk;
    sub->mail = doorUpChk_mes_l;
    ACTSendMailCorrect(self, 377);
    _ACTWait(0);
}

void actSt17aDoorUpChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt17aDoorUpChk_jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);
    long long buf[2];
    int h;

    while (scpTriggerFloorAttrTargetMan(self, 0x3000000) == 0) {
        _ACTWait(1);
    }

    _ACTWait(15);

    actCreateSubThread(actSt17aDoorUpEffect, 21);

    scpWakeupItemWithBoundary(6573.0f, -2077.0f, 1089.0f, 100.0f);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(129, 125, 109, 108), 1, 0);

    buf[0] = doorChkSePos.d[0];
    buf[1] = doorChkSePos.d[1];
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1220, 1196), 0, (float *)buf, 1);
    _ACTWait(30);

    h = soundSeDefPlay(PAL_NTSCAUG6JUN26(1221, 1197), 0, (float *)buf, 1);
    _ACTWait(60);
    soundSeDefStop(h);

    soundSeDefPlay(PAL_NTSCAUG6JUN26(1222, 1198), 0, (float *)buf, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(129, 125, 109, 108)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    stage_SetLoopFlag(PAL_JPNKORCN_NTSC_AUG6JUN26(131, 127, 111, 110), 1);
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(131, 127, 111, 110), 1, 0);

    doorUpChk_mes[0].func = actSt17aDoorDownChk;
    sub->mail = doorUpChk_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

static void actSt17aDoorDownChk_jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    static ActMail door_mes_l[2] = {{377}, {376}}; /* derived name */
    long long buf[2];
    int h;

    while (scpTriggerFloorAttrTargetMan(self, 0x3000000) != 0) {
        _ACTWait(1);
    }

    _ACTWait(15);

    actCreateSubThread(actSt17aDoorDownEffect, 21);

    scpWakeupItemWithBoundary(6573.0f, -2077.0f, 1089.0f, 100.0f);

    stage_SetAnimation(100, 1, 0);

    buf[0] = doorChkSePos.d[0];
    buf[1] = doorChkSePos.d[1];
    soundSeDefPlay(1170, 0, (float *)buf, 1);
    _ACTWait(30);

    h = soundSeDefPlay(1171, 0, (float *)buf, 1);
    _ACTWait(30);
    soundSeDefStop(h);

    soundSeDefPlay(1172, 0, (float *)buf, 1);

    while (stage_CheckAnimationFinish(100) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    door_mes_l[0].func = actSt17aDoorUpChk;
    sub->mail = door_mes_l;
    ACTSendMailCorrect(self, 377);
    _ACTWait(0);
}

void actSt17aDoorDownChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt17aDoorDownChk_jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);
    long long buf[2];
    int h;

    while (scpTriggerFloorAttrTargetMan(self, 0x3000000) != 0) {
        _ACTWait(1);
    }

    _ACTWait(15);

    actCreateSubThread(actSt17aDoorDownEffect, 21);

    scpWakeupItemWithBoundary(6573.0f, -2077.0f, 1089.0f, 100.0f);

    stage_SetLoopFlag(PAL_JPNKORCN_NTSC_AUG6JUN26(131, 127, 111, 110), 0);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(130, 126, 110, 109), 1, 0);

    buf[0] = doorChkSePos.d[0];
    buf[1] = doorChkSePos.d[1];
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1220, 1196), 0, (float *)buf, 1);
    _ACTWait(30);

    h = soundSeDefPlay(PAL_NTSCAUG6JUN26(1221, 1197), 0, (float *)buf, 1);
    _ACTWait(PAL_AUG6JUN26(38, 30));
    soundSeDefStop(h);

    soundSeDefPlay(PAL_NTSCAUG6JUN26(1222, 1198), 0, (float *)buf, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(130, 126, 110, 109)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    door_mes[0].func = actSt17aDoorUpChk;
    sub->mail = door_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

static void actSt17aHasiChk_ntsc(GObj *volatile self)
{
    long long buf[2];
    ConstVec pos;
    float dir[4];

    if ((void *)girlGObj == 0) {
        _ACTWait(0);
    }

    while (1) {
        if ((GOBJ_ACT(girlGObj)->actMode != 107 && (void *)girlGObj != 0 &&
             scpTriggerFloorAttr((void *)girlGObj, 0x1000000) != 0 &&
             scpTriggerFloorAttr(boyGObj, 0x2000000) != 0) ||
            (GOBJ_ACT(girlGObj)->actMode != 107 && (void *)girlGObj != 0 &&
             scpTriggerFloorAttr((void *)girlGObj, 0x4000000) != 0 &&
             scpTriggerFloorAttr(boyGObj, 0x2000000) != 0)) {
            break;
        }
        _ACTWait(1);
    }

    iosPadActRequest(boyPad, 15);
    gflagOn(31);
    SetWayGroupActive(3, 0);

    scpSearchGobj(220)->active = 0;
    scpSearchGobj(221)->active = 1;

    stage_SetAnimation(113, 1, 0);
    SetCameraFlag_LwsCutBack();

    actCreateSubThread(actSt17aHasiEffect, 21);

    if (ACTGame_FLAG_TETSUNAGI() == 0) {
        stage_SetAnimation(112, 1, 0);

        buf[0] = hasiChkSePos.d[0];
        buf[1] = hasiChkSePos.d[1];
        soundSeDefPlay(1266, 0, (float *)buf, 1);

        scpPlayMot((void *)girlGObj, 514);
        _ACTWait(10);
        pos = hasiChkGirlDir;
        sceVu0SubVector(dir, pos.f, test_CURRENTROOT((void *)girlGObj));
        scpPlayMotDir((void *)girlGObj, dir);
        _ACTWait(10);
        scpPlayJump((void *)girlGObj, 272);
        _ACTWait(330);
        pos = hasiChkGirlWay;
        _SCPMoveCharactorByWay((void *)girlGObj, 0, pos.f, 100.0f, 2);
    } else {
        _SCPBoySupportGirl(-3713.0f, 1907.0f, -1095.0f, -4713.0f, 1907.0f, -1095.0f);

        stage_SetAnimation(112, 1, 0);

        pos = hasiChkSePos;
        soundSeDefPlay(1266, 0, pos.f, 1);
    }

    while (stage_CheckAnimationFinish(112) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

static void actSt17aHasiChk_jun26(GObj *volatile self)
{
    ConstVec pos;
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (girlGObj == 0 ||
           (scpTriggerFloorAttr(girlGObj, 0x1000000) == 0 &&
            scpTriggerFloorAttr(girlGObj, 0x4000000) == 0) ||
           scpTriggerFloorAttr(boyGObj, 0x2000000) == 0) {
        _ACTWait(1);
    }

    iosPadActRequest(boyPad, 15);
    gflagOn(31);

    scpSearchGobj(211)->active = 0;
    scpSearchGobj(212)->active = 1;

    stage_SetAnimation(102, 1, 0);
    SetCameraFlag_LwsCutBack();

    if (ACTGame_FLAG_TETSUNAGI() == 0) {
        stage_SetAnimation(101, 1, 0);

        scpPlayMot(girlGObj, 521);
        _ACTWait(10);
        pos = hasiChkGirlDir;
        sceVu0SubVector(dir, pos.f, test_CURRENTROOT(girlGObj));
        scpPlayMotDir(girlGObj, dir);
        _ACTWait(10);
        scpPlayJump(girlGObj, 259);
    } else {
        _SCPBoySupportGirl(-3713.0f, 1907.0f, -1095.0f, -4713.0f, 1907.0f, -1095.0f);

        stage_SetAnimation(101, 1, 0);
    }

    actCreateSubThread(actSt17aHasiEffect, 21);

    pos = hasiChkSePos;
    soundSeDefPlay(1240, 0, pos.f, 1);

    while (stage_CheckAnimationFinish(101) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

static void actSt17aHasiChk_aug6(GObj *volatile self)
{
    long long buf[2];
    ConstVec pos;
    float dir[4];
    if ((void *)girlGObj == 0) {
        _ACTWait(0);
    }

    while (1) {
        if ((GOBJ_ACT(girlGObj)->actMode != 107 && (void *)girlGObj != 0 &&
             scpTriggerFloorAttr((void *)girlGObj, 0x1000000) != 0 &&
             scpTriggerFloorAttr(boyGObj, 0x2000000) != 0) ||
            (GOBJ_ACT(girlGObj)->actMode != 107 && (void *)girlGObj != 0 &&
             scpTriggerFloorAttr((void *)girlGObj, 0x4000000) != 0 &&
             scpTriggerFloorAttr(boyGObj, 0x2000000) != 0)) {
            break;
        }
        _ACTWait(1);
    }

    iosPadActRequest(boyPad, 15);
    gflagOn(31);
    SetWayGroupActive(3, 0);

    scpSearchGobj(219)->active = 0;
    scpSearchGobj(220)->active = 1;

    stage_SetAnimation(112, 1, 0);
    SetCameraFlag_LwsCutBack();

    if (ACTGame_FLAG_TETSUNAGI() == 0) {
        stage_SetAnimation(111, 1, 0);

        buf[0] = hasiChkSePos.d[0];
        buf[1] = hasiChkSePos.d[1];
        soundSeDefPlay(1266, 0, (float *)buf, 1);

        scpPlayMot((void *)girlGObj, 514);
        _ACTWait(10);
        pos = hasiChkGirlDir;
        sceVu0SubVector(dir, pos.f, test_CURRENTROOT((void *)girlGObj));
        scpPlayMotDir((void *)girlGObj, dir);
        _ACTWait(10);
        scpPlayJump((void *)girlGObj, 270);
        _ACTWait(300);
        pos = hasiChkGirlWay;
        _SCPMoveCharactorByWay((void *)girlGObj, 0, pos.f, 100.0f, 2);
    } else {
        _SCPBoySupportGirl(-3713.0f, 1907.0f, -1095.0f, -4713.0f, 1907.0f, -1095.0f);

        stage_SetAnimation(111, 1, 0);

        pos = hasiChkSePos;
        soundSeDefPlay(1266, 0, pos.f, 1);
    }

    actCreateSubThread(actSt17aHasiEffect, 21);

    while (stage_CheckAnimationFinish(111) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

void actSt17aHasiChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt17aHasiChk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt17aHasiChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt17aHasiChk_aug6(self);
        return;
    }
    if ((void *)girlGObj == 0) {
        _ACTWait(0);
    }

    while (1) {
        if ((GOBJ_ACT(girlGObj)->actMode != 111 && (void *)girlGObj != 0 &&
             scpTriggerFloorAttr((void *)girlGObj, 0x1000000) != 0 &&
             scpTriggerFloorAttr(boyGObj, 0x2000000) != 0) ||
            (GOBJ_ACT(girlGObj)->actMode != 111 && (void *)girlGObj != 0 &&
             scpTriggerFloorAttr((void *)girlGObj, 0x4000000) != 0 &&
             scpTriggerFloorAttr(boyGObj, 0x2000000) != 0)) {
            break;
        }
        _ACTWait(1);
    }

    gflagOn(33);

    iosPadActRequest(boyPad, 15);
    SetWayGroupActive(3, 0);

    scpSearchGobj(243)->active = 0;
    scpSearchGobj(244)->active = 1;

    stage_SetAnimation(PAL_JPNKORCN(133, 129), 1, 0);
    SetCameraFlag_LwsCutBack();

    actCreateSubThread(actSt17aHasiEffect, 21);

    if (ACTGame_FLAG_TETSUNAGI() == 0) {
        long long buf[2];

        scpPlayStart((void *)girlGObj);

        stage_SetAnimation(PAL_JPNKORCN(132, 128), 1, 0);

        buf[0] = hasiChkSePos.d[0];
        buf[1] = hasiChkSePos.d[1];
        soundSeDefPlay(1290, 0, (float *)buf, 1);

        scpPlayMot((void *)girlGObj, 723);
        scpPlayWaitMotEnd((void *)girlGObj);

        scpPlayMot((void *)girlGObj, 532);
        GOBJ_SUB(girlGObj)->ctrl.blendFrames =
            (int)((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f * 0.0f);

        scpPlayEnd((void *)girlGObj);

        scpBoyControlReadDisable = 0;
    } else {
        long long buf2[2];

        scpPlayStart(boyGObj);
        scpPlayStart((void *)girlGObj);

        stage_SetAnimation(PAL_JPNKORCN(132, 128), 1, 0);

        buf2[0] = hasiChkSePos.d[0];
        buf2[1] = hasiChkSePos.d[1];
        soundSeDefPlay(1290, 0, (float *)buf2, 1);

        scpPlayMot(boyGObj, 262);
        scpPlayMot((void *)girlGObj, 722);
        scpPlayWaitMotEnd(boyGObj);

        scpPlayEnd(boyGObj);
        scpPlayEnd((void *)girlGObj);

        scpPlayMot(boyGObj, 0);
        scpPlayMot((void *)girlGObj, 532);
        GOBJ_SUB(girlGObj)->ctrl.blendFrames =
            (int)((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f * 0.0f);
    }

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(132, 128)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

/* The seven spawn positions are initialised block locals, as in e3.c's
 * actE3CageFallEffect. */
void actSt17aHasiEffect(GObj *volatile self)
{
    float t;
    int n;

    t = 0.0f;
    do {
        switch ((int)t) {
        case 0: {
            EffectArg b1 = {{3645.0f, -1966.0f, 1122.0f, 1.0f}};
            scpEffectStart(&b1, 15);
        } break;
        case 15: {
            EffectArg b2 = {{3753.0f, -1936.0f, 1090.0f, 1.0f}};
            scpEffectStart(&b2, 0);
        }
            {
                EffectArg b3 = {{3605.0f, -1866.0f, 1150.0f, 1.0f}};
                scpEffectStart(&b3, 15);
            }
            break;
        case 60: {
            EffectArg b4 = {{3759.0f, -1666.0f, 1152.0f, 1.0f}};
            scpEffectStart(&b4, 15);
        }
            {
                EffectArg b5 = {{3305.0f, -1566.0f, 1082.0f, 1.0f}};
                scpEffectStart(&b5, 0);
            }
            break;
        case 120: {
            EffectArg b6 = {{3545.0f, -1466.0f, 1120.0f, 1.0f}};
            scpEffectStart(&b6, 0);
        }
            {
                EffectArg b7 = {{3655.0f, -1066.0f, 1477.0f, 1.0f}};
                scpEffectStart(&b7, 0);
            }
            break;
        }
        n = (int)t;
        t += (float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f;
        if (n != (int)t) {
            _ACTWait(1);
        } else {
            t += 1.0f;
        }
    } while (t < 1000.0f);
}

void actSt17aIntroChk(GObj *volatile self);

/* .sbss: the demo's own end flag, raised by the subthread the wait loop below
   spins for. */
static int demoEnd;

/* self is the actor entry parameter, volatile like every other stage
   actor's. */
static void actSt17aIntroCancel(GObj *volatile self)
{
    demoEnd = 0;

    while (lt_fade_status() != 2) {
        _ACTWait(1);
    }

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(PAL_JPNKORCN(83, 79), 1, -1);
        SetCameraFlag_LwsCutBack();
        scpFadeIn(3.0f);
    }

    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpBoyControlReadDisable = 0;
}

static inline void actLinkTest_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    linkTest_mes[0].func = actLinkTestChk;
    act->mail = linkTest_mes;
    ACTSendMailCorrect(self, 377);
    _ACTWait(0);
}

void actLinkTest(GObj *volatile self)
{
    if (ICO_JUN26) {
        actLinkTest_jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    linkTest_mes[0].func = actLinkTestChk;
    act->mail = linkTest_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

static inline void actSt17aSekizo_jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    scpSekizou(self, 30, 63, 100, 21, 6450.0f, -2100.0f, 1000.0f, 6450.0f, -2100.0f, 1100.0f);
}

void actSt17aSekizo(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt17aSekizo_jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (ICO_NTSC) {
        scpSekizou(self, 30, 69, 110, 18, 6450.0f, -2100.0f, 1000.0f, 6450.0f, -2100.0f, 1100.0f);
    } else if (ICO_AUG6JUN26) {
        scpSekizou(self, 30, 69, 109, 18, 6450.0f, -2100.0f, 1000.0f, 6450.0f, -2100.0f, 1100.0f);
    } else {
        scpSekizou(self, 32, PAL_JPNKORCN(82, 78), PAL_JPNKORCN(130, 126), 18, 6450.0f, -2100.0f,
                   1000.0f, 6450.0f, -2100.0f, 1100.0f);
    }
}

inline void actLinkTestChk(GObj *volatile self);

static inline void actSt17aHasi_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(31) == 0) {
        scpSearchGobj(212)->active = 0;
        stage_SetAnimation(101, 0, 0);
        hasi_mes[0].func = actSt17aHasiChk;
        act->mail = hasi_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    } else {
        scpSearchGobj(211)->active = 0;
        stage_SetAnimation(101, 0, -1);
    }
}

void actSt17aHasi(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt17aHasi_jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(33, 31)) == 0) {
        scpSearchGobj(PAL_NTSC_AUG6JUN26(244, 221, 220))->active = 0;
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(132, 128, 112, 111), 0, 0);
        SetWayGroupActive(3, 1);
        hasi_mes[0].func = actSt17aHasiChk;
        act->mail = hasi_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        scpSearchGobj(PAL_NTSC_AUG6JUN26(243, 220, 219))->active = 0;
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(132, 128, 112, 111), 0, -1);
    }
}

static inline void actSt17aIntro_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(32) == 0) {
        intro_mes[0].func = actSt17aIntroChk;
        act->mail = intro_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    }
}

void actSt17aIntro(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt17aIntro_jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(34, 32)) == 0) {
        intro_mes[0].func = actSt17aIntroChk;
        act->mail = intro_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

inline void actSt17aGirlWay(GObj *volatile self);

void actSt17aHint1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(35) == 0) {
        hint1_mes[0].func = actSt17aHint1Chk;
        act->mail = hint1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        FinishHint(0);
    }
}

void actSt17aFall(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(33) == 0) {
        fall_mes[0].func = actSt17aFallChk;
        act->mail = fall_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static inline void actSt17aSekizoEvent_jun26(int x)
{
    volatile int local = x;
}

void actSt17aSekizoEvent(int x)
{
    if (ICO_JUN26) {
        actSt17aSekizoEvent_jun26(x);
        return;
    }
    volatile int local = x;
}

static inline void actLinkTestChk_jun26(GObj *volatile self)
{
    GOBJ_SUB(boyGObj)->ctrl.noStand = 1; /* Sub15C + 0x4E8 */
    GOBJ_SUB(boyGObj)->ctrl.noStand = 0;
    scpGetWallCollision(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    _ACTWait(60);
}

void actLinkTestChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actLinkTestChk_jun26(self);
        return;
    }
    GOBJ_SUB(boyGObj)->ctrl.noStand = 1; /* Sub15C + 0x4E8 */
    GOBJ_SUB(boyGObj)->ctrl.noStand = 0;
    scpGetWallCollision(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    _ACTWait(60);
}

static inline void actSt17aDoorEvent_jun26(int x)
{
    volatile int local = x;
}

void actSt17aDoorEvent(int x)
{
    if (ICO_JUN26) {
        actSt17aDoorEvent_jun26(x);
        return;
    }
    volatile int local = x;
}

void actSt17aDoorUpChk(GObj *volatile self);
void actSt17aDoorDownChk(GObj *volatile self);
inline void actSt17aDoorUpEffect(GObj *volatile self);
void actSt17aDoorDownEffect(GObj *volatile self);

static inline void actSt17aDoorUpEffect_jun26(GObj *volatile self)
{
    long long b1[2];
    long long b2[2];
    long long v0a = doorUpEffectPos.d[0];
    long long v0b = doorDownEffectPos.d[0];
    int i;
    for (i = 0; i < 50; i++) {
        switch (i) {
        case 0:
            b1[0] = v0a;
            b1[1] = doorUpEffectPos.d[1];
            scpEffectStart((int *)b1, 0);
            break;
        case 30:
            b2[0] = v0b;
            b2[1] = doorDownEffectPos.d[1];
            scpEffectStart((int *)b2, 0);
            break;
        }
        _ACTWait(1);
    }
}

void actSt17aDoorUpEffect(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt17aDoorUpEffect_jun26(self);
        return;
    }
    long long b1[2];
    long long b2[2];
    long long v0a = doorUpEffectPos.d[0];
    long long v0b = doorDownEffectPos.d[0];
    int i;
    for (i = 0; i < 50; i++) {
        switch (i) {
        case 0:
            b1[0] = v0a;
            b1[1] = doorUpEffectPos.d[1];
            scpEffectStart((int *)b1, 0);
            break;
        case 30:
            b2[0] = v0b;
            b2[1] = doorDownEffectPos.d[1];
            scpEffectStart((int *)b2, 0);
            break;
        }
        _ACTWait(1);
    }
}

static inline void actSt17aDoorDownEffect_jun26(GObj *volatile self)
{
    long long b1[2];
    long long b2[2];
    long long v0a = doorDownEffectPos.d[0];
    long long v0b = doorDownEffect2Pos.d[0];
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
            break;
        }
        _ACTWait(1);
    }
}

void actSt17aDoorDownEffect(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt17aDoorDownEffect_jun26(self);
        return;
    }
    long long b1[2];
    long long b2[2];
    long long v0a = doorDownEffectPos.d[0];
    long long v0b = doorDownEffect2Pos.d[0];
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
            break;
        }
        _ACTWait(1);
    }
}

static inline void actSt17aHasiEvent_jun26(int x)
{
    volatile int local = x;
}

void actSt17aHasiEvent(int x)
{
    if (ICO_JUN26) {
        actSt17aHasiEvent_jun26(x);
        return;
    }
    volatile int local = x;
}

void actSt17aHasiChk(GObj *volatile self);
void actSt17aHasiEffect(GObj *volatile self);

/* .sdata: the camera stream handle. */
SqEntry *cam = 0;

static void actSt17aIntroChk_jun26(GObj *volatile self)
{
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    gflagOn(32);
    if (girlGObj != 0) {
        iosOmSendMail(girlGObj, 57, boyGObj);
        actCreateSubThread(actSt17aGirlWay, 21);
    }
    scpAdpcmPlayRequestFunc(1, &cam, 1, 1, 1);
    _ACTWait(1);
    stage_SetAnimation(64, 1, 0);
    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(64) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    if (cam != 0) {
        scpAdpcmFadeClose(cam, 80);
    }
    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
}

void actSt17aIntroChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt17aIntroChk_jun26(self);
        return;
    }
    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
    scpBoyControlReadDisable = 1;
    if (ICO_NTSC) {
        gflagOn(32);
        if (girlGObj != 0) {
            iosOmSendMail(girlGObj, 59, boyGObj);
            actCreateSubThread(actSt17aGirlWay, 21);
        }
        scpAdpcmPlayRequestFunc(51, &cam, 1, 1, 1);
    } else if (ICO_AUG6JUN26) {
        gflagOn(32);
        if (girlGObj != 0) {
            iosOmSendMail(girlGObj, 58, boyGObj);
            actCreateSubThread(actSt17aGirlWay, 21);
        }
        scpAdpcmPlayRequestFunc(51, &cam, 1, 1, 1);
    } else {
        gflagOn(34);
        scpAdpcmPlayRequestFunc(53, &cam, 1, 1, 1);
    }
    _ACTWait(1);
    stage_SetAnimation(PAL_JPNKORCN_NTSCAUG6JUN26(83, 79, 70), 1, 0);
    SetCameraFlag_LwsCutBack();
    if (!ICO_NTSCAUG6JUN26) {
        actCreateSubThread(actSt17aIntroCancel, 21);
    }

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSCAUG6JUN26(83, 79, 70)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    if (ICO_NTSCAUG6JUN26) {
        lt_switch_layout(50);
        scpBoyControlReadDisable = 0;
    } else {
        demoEnd = 1;
    }
}

static inline void actSt17aGirlWay_jun26(GObj *volatile self)
{
    EffectArg buf = {{1547.0f, -2070.0f, 1495.0f, 0.0f}};

    _SCPMoveCharactorByWay((void *)girlGObj, 0, buf.f, 100.0f, 2);
}

void actSt17aGirlWay(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt17aGirlWay_jun26(self);
        return;
    }
    EffectArg buf = {{1547.0f, -2070.0f, 1495.0f, 0.0f}};

    _SCPMoveCharactorByWay((void *)girlGObj, 0, buf.f, 100.0f, 2);
}

static void actSt17aHint1Chk(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    debug_StdPrintfDummy("HINT1_FINISH!!!!!!!!!!!!!!!\n");

    gflagOn(35);
    FinishHint(0);
}

static void actSt17aFallChk(GObj *volatile self)
{
    while (!(gflagChk(33) && scpTriggerBall(self, boyGObj, 1800.0f))) {
        _ACTWait(1);
    }

    debug_StdPrintfDummy("FAAAAALL!\n");

    iosOmSendMail(boyGObj, 226, boyGObj);
}
