#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
#include "s_init.h"
#include "act.h"
#include "gobj_process.h"
#include "commonact.h"
#include "way_llf.h"
#include "fightSound.h"
#include "generator.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "item.h"
#include "typedef.h"
#include "main.h"
#include "script.h"

/* .sdata: the prototype stair's stream handle, its shake and the shake's
   volume. */
SqEntry *proto = 0;

unsigned int proto_yure = 0;

unsigned char proto_yure_vol = 0;

/* .rodata: six 16-byte constant vectors, each named for the first actor that
   copies it.  The float view carries the values, the long long view is the
   one the copies read. */

static const ConstVec door2UpChkPos = {{0.0f, -422.0f, 1630.0f, 0.0f}}; /* derived name */

static const ConstVec door2UpEffectPos = {{0.0f, -150.0f, 1640.0f, 1.0f}}; /* derived name */

static const ConstVec door2UpEffect2Pos = {{0.0f, -450.0f, 1640.0f, 1.0f}}; /* derived name */

static const ConstVec door1UpChkPos = {{-1.0f, -184.0f, -57.0f, 0.0f}}; /* derived name */

static const ConstVec door1UpEffectPos = {{-505.0f, -1200.0f, -5671.0f, 1.0f}}; /* derived name */

static const ConstVec door1UpEffect2Pos = {{-505.0f, -1447.0f, -5671.0f, 1.0f}}; /* derived name */

/* .data: eleven actor mail records, each the usual pair, the 430 entry whose
   handler the sender fills in and the 429 terminator, named for the actor that
   installs it. */
static ActMail atr2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail atr2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail atr2_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define atr2_mes PAL_NTSC_AUG6JUN26(atr2_mes_pal, atr2_mes_ntsc, atr2_mes_aug6jun26)

static ActMail ene_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail ene_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail ene_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define ene_mes PAL_NTSC_AUG6JUN26(ene_mes_pal, ene_mes_ntsc, ene_mes_aug6jun26)

static ActMail stair_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail stair_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail stair_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define stair_mes PAL_NTSC_AUG6JUN26(stair_mes_pal, stair_mes_ntsc, stair_mes_aug6jun26)

static ActMail door2Down_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door2Down_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door2Down_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define door2Down_mes                                                                              \
    PAL_NTSC_AUG6JUN26(door2Down_mes_pal, door2Down_mes_ntsc, door2Down_mes_aug6jun26)

static ActMail door2Up_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door2Up_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door2Up_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define door2Up_mes PAL_NTSC_AUG6JUN26(door2Up_mes_pal, door2Up_mes_ntsc, door2Up_mes_aug6jun26)

/* the four door checks' records */
static ActMail door2Upchk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door2Upchk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door2Upchk_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define door2Upchk_mes                                                                             \
    PAL_NTSC_AUG6JUN26(door2Upchk_mes_pal, door2Upchk_mes_ntsc, door2Upchk_mes_aug6jun26)

static ActMail door2Downchk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door2Downchk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door2Downchk_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define door2Downchk_mes                                                                           \
    PAL_NTSC_AUG6JUN26(door2Downchk_mes_pal, door2Downchk_mes_ntsc, door2Downchk_mes_aug6jun26)

static ActMail door1Down_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door1Down_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door1Down_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define door1Down_mes                                                                              \
    PAL_NTSC_AUG6JUN26(door1Down_mes_pal, door1Down_mes_ntsc, door1Down_mes_aug6jun26)

static ActMail door1Up_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door1Up_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door1Up_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define door1Up_mes PAL_NTSC_AUG6JUN26(door1Up_mes_pal, door1Up_mes_ntsc, door1Up_mes_aug6jun26)

static ActMail door1Upchk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door1Upchk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door1Upchk_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define door1Upchk_mes                                                                             \
    PAL_NTSC_AUG6JUN26(door1Upchk_mes_pal, door1Upchk_mes_ntsc, door1Upchk_mes_aug6jun26)

static ActMail door1Downchk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door1Downchk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door1Downchk_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define door1Downchk_mes                                                                           \
    PAL_NTSC_AUG6JUN26(door1Downchk_mes_pal, door1Downchk_mes_ntsc, door1Downchk_mes_aug6jun26)

static void actSt00aInit_ntsc(void)
{
    if (gflagChk(38) == 0) {
        stage_SetAnimation(77, 0, 0);
        stage_SetAnimation(74, 0, 0);
    } else {
        stage_SetAnimation(75, 0, -1);
        stage_SetAnimation(74, 0, -1);
    }
}

static void actSt00aInit_aug6jun26(void)
{
    if (gflagChk(38) == 0) {
        stage_SetAnimation(76, 0, 0);
        stage_SetAnimation(73, 0, 0);
    } else {
        stage_SetAnimation(74, 0, -1);
        stage_SetAnimation(73, 0, -1);
    }
}

void actSt00aInit(void)
{
    if (ICO_NTSC) {
        actSt00aInit_ntsc();
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt00aInit_aug6jun26();
        return;
    }
    if (gflagChk(41) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(90, 86), 0, 0);
        stage_SetAnimation(PAL_JPNKORCN(87, 83), 0, 0);
        SetWayGroupActive(3, 0);
        SetWayGroupActive(13, 1);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(88, 84), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(87, 83), 0, -1);
        FinishHint(1);
        SetWayGroupActive(3, 1);
        SetWayGroupActive(13, 0);
    }
    if (gflagChk(79) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(96, 92), 0, 1);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(96, 92), 0, 2);
    }
}

void actSt00aEnd(void)
{
    if (girlGObj != 0) {
        if (gflagChk(PAL_NTSCAUG6JUN26(42, 39)) == 0) {
            gflagOn(PAL_NTSC_AUG6JUN26(391, 358, 357));
        }
    }
}

void actSt00aEneChk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (gflagChk(PAL_NTSCAUG6JUN26(42, 39)) == 0 || gflagChk(PAL_NTSCAUG6JUN26(38, 35)) != 0) {
        _ACTWait(1);
    }
    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
    scpBoyControlReadDisable = 1;
    if (ICO_NTSC) {
        gflagOn(36);
        scpSleepEnemyOne(3453);
        gflagOff(358);
    } else if (ICO_AUG6JUN26) {
        gflagOn(36);
        scpSleepEnemyOne(3415);
        gflagOff(357);
    } else {
        gflagOn(39);
        scpSleepEnemyOne(PAL_JPNKORCN(3757, 3734));
        scpSleepEnemyAll();
        gflagOff(391);
    }
    _ACTWait(60);
    if (ICO_NTSC) {
        gflagOn(37);
        stage_SetAnimation(156, 1, 0);
    } else if (ICO_AUG6JUN26) {
        gflagOn(37);
        stage_SetAnimation(155, 1, 0);
    } else {
        gflagOn(40);
        stage_SetAnimation(PAL_JPNKORCN(177, 173), 1, 0);
    }
    _ACTWait(150);
    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(177, 173, 156, 155)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));
    _ACTWait(120);
    scpBoyControlReadDisable = 0;
    if (ICO_NTSC) {
        gflagOff(36);
        scpWakeupEnemyOne(3453);
    } else if (ICO_AUG6JUN26) {
        gflagOff(36);
        scpWakeupEnemyOne(3415);
    } else {
        scpWakeupEnemyOne(PAL_JPNKORCN(3757, 3734));
        scpWakeupEnemyAll();
        _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1]);
        gflagOff(39);
    }
}

/* .sbss: the flag the stair-check subthread raises when the demo is over,
   which the wait loop below spins for. */
static int demoEnd;

static void actSt00aStairChkSub(GObj *volatile self);

static void actSt00aStairChk_ntsc(GObj *volatile self)
{
    while (scpTriggerBall(self, scpSearchGobj(249), 90.0f) != 0 || gflagChk(36) != 0) {
        _ACTWait(1);
    }
    gflagOn(35);
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    gflagOn(38);
    scpSleepEnemyAll();
    _ACTWait(60);
    fightSoundProcessRequestPause();
    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }
    scpAdpcmPlayRequestFunc(20, &proto, 1, 1, 1);
    while (proto == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(74, 1, 0);
    ReviveAllCarryableItemsWithNonSleepFrame(260);
    _ACTWait(90);
    iosPadActRequest(boyPad, 17);
    while (stage_CheckAnimationFinish(74) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    stage_SetAnimation(76, 1, 0);
    stage_SetAnimation(77, 1, 0);
    proto_yure = iosPadActRequest(boyPad, 9);
    proto_yure_vol = 128;
    iosPadActVolumeSet(proto_yure, 128);
    _ACTWait(500);
    iosPadActStop(proto_yure);
    while (stage_CheckAnimationFinish(77) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpSearchGobj(246)->active = 0;
    scpSearchGobj(245)->active = 1;
    lt_switch_layout(50);
    _ACTWait(120);
    ReviveAllCarryableItemsWithNonSleepFrame(60);
    stage_SetAnimation(75, 0, -1);
    stage_SetAnimation(77, -1, -2);
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
    gflagOff(35);
    fightSoundProcessRequestStart();
}

static void actSt00aStairChk_aug6jun26(GObj *volatile self)
{
    while (scpTriggerBall(self, scpSearchGobj(248), 90.0f) != 0 || gflagChk(36) != 0) {
        _ACTWait(1);
    }
    gflagOn(35);
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    gflagOn(38);
    scpSleepEnemyAll();
    _ACTWait(60);
    fightSoundProcessRequestPause();
    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }
    scpAdpcmPlayRequestFunc(20, &proto, 1, 1, 1);
    while (proto == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(73, 1, 0);
    ReviveAllCarryableItemsWithNonSleepFrame(260);
    _ACTWait(90);
    iosPadActRequest(boyPad, 17);
    while (stage_CheckAnimationFinish(73) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    stage_SetAnimation(75, 1, 0);
    stage_SetAnimation(76, 1, 0);
    proto_yure = iosPadActRequest(boyPad, 9);
    proto_yure_vol = 128;
    iosPadActVolumeSet(proto_yure, 128);
    _ACTWait(500);
    iosPadActStop(proto_yure);
    while (stage_CheckAnimationFinish(76) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpSearchGobj(245)->active = 0;
    scpSearchGobj(244)->active = 1;
    lt_switch_layout(50);
    _ACTWait(120);
    ReviveAllCarryableItemsWithNonSleepFrame(60);
    stage_SetAnimation(74, 0, -1);
    stage_SetAnimation(76, -1, -2);
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
    gflagOff(35);
    fightSoundProcessRequestStart();
}

void actSt00aStairChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt00aStairChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt00aStairChk_aug6jun26(self);
        return;
    }
    GProc *th;
    int fade;

    while (scpTriggerBall(self, scpSearchGobj(276), 90.0f) != 0 || gflagChk(39) != 0) {
        _ACTWait(1);
    }
    gflagOn(38);
    FinishHint(1);
    SetWayGroupActive(3, 1);
    SetWayGroupActive(13, 0);
    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    gflagOn(41);
    scpSleepEnemyAll();
    _ACTWait(60);
    fightSoundProcessRequestPause();
    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }
    scpAdpcmPlayRequestFunc(21, &proto, 1, 1, 1);
    while (proto == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(PAL_JPNKORCN(87, 83), 1, 0);
    ReviveAllCarryableItemsWithNonSleepFrame(260);
    proto_yure = 0xFFFFFFFF;
    th = actCreateSubThread(actSt00aStairChkSub, 21);
    demoEnd = 0;
    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }
    fade = demoEnd ^ 1;
    if (fade) {
        scpAdpcmFadeCloseFunc(&proto, 256);
        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    }
    iosThreadSetPri(&th->thread, 34);
    if (fade) {
        stage_SetAnimation(PAL_JPNKORCN(90, 86), 1, -1);
        stage_SetAnimation(PAL_JPNKORCN(87, 83), 0, -1);
        scpFadeIn(3.0f);
    }
    iosPadActStop(proto_yure);
    while (stage_CheckAnimationFinish(PAL_JPNKORCN(90, 86)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpSearchGobj(273)->active = 0;
    scpSearchGobj(272)->active = 1;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
    _ACTWait(120);
    ReviveAllCarryableItemsWithNonSleepFrame(60);
    stage_SetAnimation(PAL_JPNKORCN(88, 84), 0, -1);
    stage_SetAnimation(PAL_JPNKORCN(90, 86), -1, -2);
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
    gflagOff(38);
    fightSoundProcessRequestStart();
}

void actSt00aDoor2DownChk(GObj *self);
void actSt00aDoor2UpChk(GObj *self);

void actSt00aDoor2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (scpTriggerBall(self, boyGObj, 200.0f) != 0 ||
        (girlGObj != 0 && scpTriggerBall(self, girlGObj, 400.0f) != 0)) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(94, 90, 81, 80), 0, 0);
        _ACTWait(60);
        door2Down_mes[0].func = actSt00aDoor2DownChk;
        act->mail = door2Down_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(93, 89, 80, 79), 0, 0);
        door2Up_mes[0].func = actSt00aDoor2UpChk;
        act->mail = door2Up_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt00aDoor2UpEffect(GObj *volatile self);

void actSt00aDoor2UpChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    long long buf[2];
    int h;

    while (scpTriggerFloorAttrTargetMan(self, 0x3000000) == 0) {
        _ACTWait(1);
    }
    _ACTWait(15);

    actCreateSubThread(actSt00aDoor2UpEffect, 21);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(93, 89, 80, 79), 1, 0);

    scpWakeupItemWithBoundary(-6.0f, -221.0f, 1504.0f, 100.0f);

    _ACTWait(1);

    buf[0] = door2UpChkPos.d[0];
    buf[1] = door2UpChkPos.d[1];
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1220, 1196), 0, (float *)buf, 1);
    _ACTWait(30);
    h = soundSeDefPlay(PAL_NTSCAUG6JUN26(1221, 1197), 0, (float *)buf, 1);
    _ACTWait(60);
    soundSeDefStop(h);
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1222, 1198), 0, (float *)buf, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(93, 89, 80, 79)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    door2Upchk_mes[0].func = actSt00aDoor2DownChk;
    sub->mail = door2Upchk_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

void actSt00aDoor2DownEffect(GObj *volatile self);

void actSt00aDoor2DownChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    ConstVec pos;
    int h;

    while (1) {
        if (scpTriggerFloorAttrTargetMan(self, 0x3000000) == 0)
            break;
        _ACTWait(1);
    }
    _ACTWait(15);

    actCreateSubThread(actSt00aDoor2DownEffect, 21);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(94, 90, 81, 80), 1, 0);

    scpWakeupItemWithBoundary(-6.0f, -221.0f, 1504.0f, 100.0f);

    _ACTWait(1);

    pos = door2UpChkPos;
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1220, 1196), 0, pos.f, 1);
    _ACTWait(30);
    h = soundSeDefPlay(PAL_NTSCAUG6JUN26(1221, 1197), 0, pos.f, 1);
    _ACTWait(PAL_AUG6JUN26(37, 45));
    soundSeDefStop(h);
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1222, 1198), 0, pos.f, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(94, 90, 81, 80)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    door2Downchk_mes[0].func = actSt00aDoor2UpChk;
    sub->mail = door2Downchk_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

void actSt00aDoor1DownChk(GObj *self);
void actSt00aDoor1UpChk(GObj *self);

void actSt00aDoor1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (scpTriggerBall(self, boyGObj, 200.0f) != 0 ||
        (girlGObj != 0 && scpTriggerBall(self, girlGObj, 400.0f) != 0)) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(92, 88, 79, 78), 0, 0);
        _ACTWait(60);
        door1Down_mes[0].func = actSt00aDoor1DownChk;
        act->mail = door1Down_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(91, 87, 78, 77), 0, 0);
        door1Up_mes[0].func = actSt00aDoor1UpChk;
        act->mail = door1Up_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt00aDoor1UpEffect(GObj *volatile self);

void actSt00aDoor1UpChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    ConstVec pos;
    int h;

    while (1) {
        if (scpTriggerFloorAttrTargetMan(self, 0x4000000) != 0)
            break;
        _ACTWait(1);
    }
    _ACTWait(15);

    actCreateSubThread(actSt00aDoor1UpEffect, 21);

    scpWakeupItemWithBoundary(-8.0f, -73.0f, 64.0f, 100.0f);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(91, 87, 78, 77), 1, 0);

    pos = door1UpChkPos;
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1220, 1196), 0, pos.f, 1);
    _ACTWait(30);
    h = soundSeDefPlay(PAL_NTSCAUG6JUN26(1221, 1197), 0, pos.f, 1);
    _ACTWait(30);
    soundSeDefStop(h);
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1222, 1198), 0, pos.f, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(91, 87, 78, 77)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    door1Upchk_mes[0].func = actSt00aDoor1DownChk;
    sub->mail = door1Upchk_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

void actSt00aDoor1DownEffect(GObj *volatile self);

void actSt00aDoor1DownChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    long long buf[2];
    int h;

    while (scpTriggerFloorAttrTargetMan(self, 0x4000000) != 0) {
        _ACTWait(1);
    }
    _ACTWait(15);

    actCreateSubThread(actSt00aDoor1DownEffect, 21);

    scpWakeupItemWithBoundary(-8.0f, -73.0f, 64.0f, 100.0f);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(92, 88, 79, 78), 1, 0);

    buf[0] = door1UpChkPos.d[0];
    buf[1] = door1UpChkPos.d[1];
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1220, 1196), 0, (float *)buf, 1);
    _ACTWait(30);
    h = soundSeDefPlay(PAL_NTSCAUG6JUN26(1221, 1197), 0, (float *)buf, 1);
    _ACTWait(30);
    soundSeDefStop(h);
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1222, 1198), 0, (float *)buf, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(92, 88, 79, 78)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    door1Downchk_mes[0].func = actSt00aDoor1UpChk;
    sub->mail = door1Downchk_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

void actSt00aEne(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(42, 39)) == 0) {
        ene_mes[0].func = actSt00aEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt00aEnemy1(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(PAL_NTSCAUG6JUN26(40, 37)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(50);
    Generator_MaskOff(self);
    Generator_Call(self);
    _ACTWait(30);
    Generator_Call(self);
    _ACTWait(30);
    Generator_Call(self);
}

void actSt00aEnemy2(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(PAL_NTSCAUG6JUN26(40, 37)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(160);
    Generator_MaskOff(self);
    Generator_Call(self);
    _ACTWait(30);
    Generator_Call(self);
}

void actSt00aStair(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(41, 38)) == 0) {
        scpSearchGobj(PAL_NTSC_AUG6JUN26(272, 245, 244))->active = 0;
        stair_mes[0].func = actSt00aStairChk;
        act->mail = stair_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        scpSearchGobj(PAL_NTSC_AUG6JUN26(273, 246, 245))->active = 0;
    }
}

void actSt00aAtr2Chk(GObj *volatile self);

void actSt00aAtr2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(42, 39)) == 0) {
        atr2_mes[0].func = actSt00aAtr2Chk;
        act->mail = atr2_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt00aAtr2Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerFloorAttr(girlGObj, 0x2000000) == 0) {
        _ACTWait(1);
    }
    gflagOn(PAL_NTSCAUG6JUN26(42, 39));
}

static void actSt00aStairChkSub(GObj *volatile self)
{
    _ACTWait(90);
    iosPadActRequest(boyPad, 17);
    while (stage_CheckAnimationFinish(PAL_JPNKORCN(87, 83)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    stage_SetAnimation(PAL_JPNKORCN(89, 85), 1, 0);
    stage_SetAnimation(PAL_JPNKORCN(90, 86), 1, 0);
    proto_yure = iosPadActRequest(boyPad, 9);
    proto_yure_vol = 128;
    iosPadActVolumeSet(proto_yure, 128);
    _ACTWait(500);
    demoEnd = 1;
    _ACTWait(0);
}

void actSt00aDoor2Event(int x)
{
    volatile int local = x;
}

void actSt00aDoor2UpEffect(GObj *volatile self)
{
    long long b1[2];
    long long b2[2];
    long long v0a = door2UpEffectPos.d[0];
    long long v0b = door2UpEffect2Pos.d[0];
    int i;
    for (i = 0; i < 50; i++) {
        switch (i) {
        case 0:
            b1[0] = v0a;
            b1[1] = door2UpEffectPos.d[1];
            scpEffectStart((int *)b1, 0);
            break;
        case 30:
            b2[0] = v0b;
            b2[1] = door2UpEffect2Pos.d[1];
            scpEffectStart((int *)b2, 0);
            break;
        }
        _ACTWait(1);
    }
}

void actSt00aDoor2DownEffect(GObj *volatile self)
{
    long long b1[2];
    long long b2[2];
    long long v0a = door2UpEffect2Pos.d[0];
    long long v0b = door2UpEffectPos.d[0];
    int i;
    for (i = 0; i < 50; i++) {
        switch (i) {
        case 0:
            b1[0] = v0a;
            b1[1] = door2UpEffect2Pos.d[1];
            scpEffectStart((int *)b1, 0);
            break;
        case 30:
            b2[0] = v0b;
            b2[1] = door2UpEffectPos.d[1];
            scpEffectStart((int *)b2, 0);
            break;
        }
        _ACTWait(1);
    }
}

void actSt00aDoor1Event(int x)
{
    volatile int local = x;
}

void actSt00aDoor1UpEffect(GObj *volatile self)
{
    long long b1[2];
    long long b2[2];
    long long v0a = door1UpEffectPos.d[0];
    long long v0b = door1UpEffect2Pos.d[0];
    int i;
    for (i = 0; i < 50; i++) {
        switch (i) {
        case 0:
            b1[0] = v0a;
            b1[1] = door1UpEffectPos.d[1];
            scpEffectStart((int *)b1, 0);
            break;
        case 30:
            b2[0] = v0b;
            b2[1] = door1UpEffect2Pos.d[1];
            scpEffectStart((int *)b2, 0);
            break;
        }
        _ACTWait(1);
    }
}

void actSt00aDoor1DownEffect(GObj *volatile self)
{
    long long b1[2];
    long long b2[2];
    long long v0a = door1UpEffect2Pos.d[0];
    long long v0b = door1UpEffectPos.d[0];
    int i;
    for (i = 0; i < 50; i++) {
        switch (i) {
        case 0:
            b1[0] = v0a;
            b1[1] = door1UpEffect2Pos.d[1];
            scpEffectStart((int *)b1, 0);
            break;
        case 30:
            b2[0] = v0b;
            b2[1] = door1UpEffectPos.d[1];
            scpEffectStart((int *)b2, 0);
            break;
        }
        _ACTWait(1);
    }
}
