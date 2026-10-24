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
static ActMail atr2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail ene_mes[2] = {{430}, {429}}; /* derived name */

static ActMail stair_mes[2] = {{430}, {429}}; /* derived name */

static ActMail door2Down_mes[2] = {{430}, {429}}; /* derived name */

static ActMail door2Up_mes[2] = {{430}, {429}}; /* derived name */

/* the four door checks' records */
static ActMail door2Upchk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail door2Downchk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail door1Down_mes[2] = {{430}, {429}}; /* derived name */

static ActMail door1Up_mes[2] = {{430}, {429}}; /* derived name */

static ActMail door1Upchk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail door1Downchk_mes[2] = {{430}, {429}}; /* derived name */

void actSt00aInit(void)
{
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
        if (gflagChk(42) == 0) {
            gflagOn(391);
        }
    }
}

void actSt00aEneChk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (gflagChk(42) == 0 || gflagChk(38) != 0) {
        _ACTWait(1);
    }
    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    gflagOn(39);
    scpSleepEnemyOne(PAL_JPNKORCN(3757, 3734));
    scpSleepEnemyAll();
    gflagOff(391);
    _ACTWait(60);
    gflagOn(40);
    stage_SetAnimation(PAL_JPNKORCN(177, 173), 1, 0);
    _ACTWait(150);
    while (stage_CheckAnimationFinish(PAL_JPNKORCN(177, 173)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    lt_switch_layout(PAL_JPNKORCN(54, 51));
    _ACTWait(120);
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyOne(PAL_JPNKORCN(3757, 3734));
    scpWakeupEnemyAll();
    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1]);
    gflagOff(39);
}

/* .sbss: the flag the stair-check subthread raises when the demo is over,
   which the wait loop below spins for. */
static int demoEnd;

static void actSt00aStairChkSub(GObj *volatile self);

void actSt00aStairChk(GObj *volatile self)
{
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
        stage_SetAnimation(PAL_JPNKORCN(94, 90), 0, 0);
        _ACTWait(60);
        door2Down_mes[0].func = actSt00aDoor2DownChk;
        act->mail = door2Down_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(93, 89), 0, 0);
        door2Up_mes[0].func = actSt00aDoor2UpChk;
        act->mail = door2Up_mes;
        ACTSendMailCorrect(self, 430);
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

    stage_SetAnimation(PAL_JPNKORCN(93, 89), 1, 0);

    scpWakeupItemWithBoundary(-6.0f, -221.0f, 1504.0f, 100.0f);

    _ACTWait(1);

    buf[0] = door2UpChkPos.d[0];
    buf[1] = door2UpChkPos.d[1];
    soundSeDefPlay(1220, 0, (float *)buf, 1);
    _ACTWait(30);
    h = soundSeDefPlay(1221, 0, (float *)buf, 1);
    _ACTWait(60);
    soundSeDefStop(h);
    soundSeDefPlay(1222, 0, (float *)buf, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(93, 89)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    door2Upchk_mes[0].func = actSt00aDoor2DownChk;
    sub->mail = door2Upchk_mes;
    ACTSendMailCorrect(self, 430);
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

    stage_SetAnimation(PAL_JPNKORCN(94, 90), 1, 0);

    scpWakeupItemWithBoundary(-6.0f, -221.0f, 1504.0f, 100.0f);

    _ACTWait(1);

    pos = door2UpChkPos;
    soundSeDefPlay(1220, 0, pos.f, 1);
    _ACTWait(30);
    h = soundSeDefPlay(1221, 0, pos.f, 1);
    _ACTWait(37);
    soundSeDefStop(h);
    soundSeDefPlay(1222, 0, pos.f, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(94, 90)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    door2Downchk_mes[0].func = actSt00aDoor2UpChk;
    sub->mail = door2Downchk_mes;
    ACTSendMailCorrect(self, 430);
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
        stage_SetAnimation(PAL_JPNKORCN(92, 88), 0, 0);
        _ACTWait(60);
        door1Down_mes[0].func = actSt00aDoor1DownChk;
        act->mail = door1Down_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(91, 87), 0, 0);
        door1Up_mes[0].func = actSt00aDoor1UpChk;
        act->mail = door1Up_mes;
        ACTSendMailCorrect(self, 430);
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

    stage_SetAnimation(PAL_JPNKORCN(91, 87), 1, 0);

    pos = door1UpChkPos;
    soundSeDefPlay(1220, 0, pos.f, 1);
    _ACTWait(30);
    h = soundSeDefPlay(1221, 0, pos.f, 1);
    _ACTWait(30);
    soundSeDefStop(h);
    soundSeDefPlay(1222, 0, pos.f, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(91, 87)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    door1Upchk_mes[0].func = actSt00aDoor1DownChk;
    sub->mail = door1Upchk_mes;
    ACTSendMailCorrect(self, 430);
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

    stage_SetAnimation(PAL_JPNKORCN(92, 88), 1, 0);

    buf[0] = door1UpChkPos.d[0];
    buf[1] = door1UpChkPos.d[1];
    soundSeDefPlay(1220, 0, (float *)buf, 1);
    _ACTWait(30);
    h = soundSeDefPlay(1221, 0, (float *)buf, 1);
    _ACTWait(30);
    soundSeDefStop(h);
    soundSeDefPlay(1222, 0, (float *)buf, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(92, 88)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    door1Downchk_mes[0].func = actSt00aDoor1UpChk;
    sub->mail = door1Downchk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt00aEne(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(42) == 0) {
        ene_mes[0].func = actSt00aEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt00aEnemy1(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(40) == 0) {
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
    while (gflagChk(40) == 0) {
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

    if (gflagChk(41) == 0) {
        scpSearchGobj(272)->active = 0;
        stair_mes[0].func = actSt00aStairChk;
        act->mail = stair_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        scpSearchGobj(273)->active = 0;
    }
}

void actSt00aAtr2Chk(GObj *volatile self);

void actSt00aAtr2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(42) == 0) {
        atr2_mes[0].func = actSt00aAtr2Chk;
        act->mail = atr2_mes;
        ACTSendMailCorrect(self, 430);
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
    gflagOn(42);
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
