#include "st04d.h"
#include "s_init.h"
#include "act.h"
#include "commonact.h"
#include "script.h"
#include "StageAnimation.h"
#include "typedef.h"
#include "main.h"

inline void actSt04dDoor1Event(int x)
{
    volatile int local = x;
}

static ActMail door1_down_mes[2] = {{430}, {429}}; /* derived name */

static ActMail door1_up_mes[2] = {{430}, {429}}; /* derived name */

void actSt04dDoor1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (scpTriggerBall(self, boyGObj, 400.0f) != 0 ||
        (girlGObj != 0 && scpTriggerBall(self, girlGObj, 400.0f) != 0)) {
        stage_SetAnimation(PAL_JPN(255, 251), 0, 0);
        _ACTWait(60);
        door1_down_mes[0].func = actSt04dDoor1DownChk;
        act->mail = door1_down_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPN(254, 250), 0, 0);
        door1_up_mes[0].func = actSt04dDoor1UpChk;
        act->mail = door1_up_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

inline void actSt04dDoor1UpEffect(GObj *volatile self);

static const ConstVec door1SoundPos = {{-498.0f, -1418.0f, -5663.0f, 0.0f}}; /* derived name */

static const ConstVec door1UpEffectPos = {{-505.0f, -1200.0f, -5671.0f, 1.0f}}; /* derived name */

static const ConstVec door1DownEffectPos = {{-505.0f, -1447.0f, -5671.0f, 1.0f}}; /* derived name */

static ActMail door1_up_chk_mes[2] = {{430}, {429}}; /* derived name */

void actSt04dDoor1UpChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    long long buf[2];

    while (scpTriggerFloorAttrTargetMan(self, 0x1000000) == 0) {
        _ACTWait(1);
    }
    _ACTWait(15);

    actCreateSubThread(actSt04dDoor1UpEffect, 21);

    stage_SetAnimation(PAL_JPN(254, 250), 1, 0);

    buf[0] = door1SoundPos.d[0];
    buf[1] = door1SoundPos.d[1];
    soundSeDefPlay(1220, 0, (float *)buf, 1);
    _ACTWait(30);
    soundSeDefPlay(1221, 0, (float *)buf, 1);
    _ACTWait(30);
    soundSeDefPlay(1222, 0, (float *)buf, 1);

    while (stage_CheckAnimationFinish(PAL_JPN(254, 250)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    door1_up_chk_mes[0].func = actSt04dDoor1DownChk;
    sub->mail = door1_up_chk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

inline void actSt04dDoor1UpEffect(GObj *volatile self)
{
    long long b1[2];
    long long b2[2];
    long long v0a = door1UpEffectPos.d[0];
    long long v0b = door1DownEffectPos.d[0];
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
            b2[1] = door1DownEffectPos.d[1];
            scpEffectStart((int *)b2, 0);
            break;
        }
        _ACTWait(1);
    }
}

inline void actSt04dDoor1DownEffect(GObj *volatile self)
{
    long long b1[2];
    long long b2[2];
    long long v0a = door1DownEffectPos.d[0];
    long long v0b = door1UpEffectPos.d[0];
    int i;
    for (i = 0; i < 50; i++) {
        switch (i) {
        case 0:
            b1[0] = v0a;
            b1[1] = door1DownEffectPos.d[1];
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

static ActMail door1_down_chk_mes[2] = {{430}, {429}}; /* derived name */

void actSt04dDoor1DownChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    long long buf[2];

    while (scpTriggerFloorAttrTargetMan(self, 0x1000000) != 0) {
        _ACTWait(1);
    }
    _ACTWait(15);

    actCreateSubThread(actSt04dDoor1DownEffect, 21);

    stage_SetAnimation(PAL_JPN(255, 251), 1, 0);

    buf[0] = door1SoundPos.d[0];
    buf[1] = door1SoundPos.d[1];
    soundSeDefPlay(1220, 0, (float *)buf, 1);
    _ACTWait(30);
    soundSeDefPlay(1221, 0, (float *)buf, 1);
    _ACTWait(30);
    soundSeDefPlay(1222, 0, (float *)buf, 1);

    while (stage_CheckAnimationFinish(PAL_JPN(255, 251)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    door1_down_chk_mes[0].func = actSt04dDoor1UpChk;
    sub->mail = door1_down_chk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

inline void actSt04dDoor2Event(int x)
{
    volatile int local = x;
}

static ActMail door2_down_mes[2] = {{430}, {429}}; /* derived name */

static ActMail door2_up_mes[2] = {{430}, {429}}; /* derived name */

void actSt04dDoor2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (scpTriggerBall(self, boyGObj, 400.0f) != 0 ||
        (girlGObj != 0 && scpTriggerBall(self, girlGObj, 400.0f) != 0)) {
        stage_SetAnimation(PAL_JPN(257, 253), 0, 0);
        _ACTWait(60);
        door2_down_mes[0].func = actSt04dDoor2DownChk;
        act->mail = door2_down_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPN(256, 252), 0, 0);
        door2_up_mes[0].func = actSt04dDoor2UpChk;
        act->mail = door2_up_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

inline void actSt04dDoor2UpEffect(GObj *volatile self);

static const ConstVec door2SoundPos = {{702.0f, -1886.0f, -5680.0f, 0.0f}}; /* derived name */

static const ConstVec door2UpEffectPos = {{704.0f, -1700.0f, -5679.0f, 1.0f}}; /* derived name */

static const ConstVec door2DownEffectPos = {{704.0f, -1955.0f, -5679.0f, 1.0f}}; /* derived name */

static ActMail door2_up_chk_mes[2] = {{430}, {429}}; /* derived name */

void actSt04dDoor2UpChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    long long buf[2];

    while (scpTriggerFloorAttrTargetMan(self, 0x2000000) == 0) {
        _ACTWait(1);
    }
    _ACTWait(15);

    actCreateSubThread(actSt04dDoor2UpEffect, 21);

    stage_SetAnimation(PAL_JPN(256, 252), 1, 0);

    buf[0] = door2SoundPos.d[0];
    buf[1] = door2SoundPos.d[1];
    soundSeDefPlay(1220, 0, (float *)buf, 1);
    _ACTWait(30);
    soundSeDefPlay(1221, 0, (float *)buf, 1);
    _ACTWait(30);
    soundSeDefPlay(1222, 0, (float *)buf, 1);

    while (stage_CheckAnimationFinish(PAL_JPN(256, 252)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    door2_up_chk_mes[0].func = actSt04dDoor2DownChk;
    sub->mail = door2_up_chk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

inline void actSt04dDoor2UpEffect(GObj *volatile self)
{
    long long b1[2];
    long long b2[2];
    long long v0a = door2UpEffectPos.d[0];
    long long v0b = door2DownEffectPos.d[0];
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
            b2[1] = door2DownEffectPos.d[1];
            scpEffectStart((int *)b2, 0);
            break;
        }
        _ACTWait(1);
    }
}

inline void actSt04dDoor2DownEffect(GObj *volatile self)
{
    long long b1[2];
    long long b2[2];
    long long v0a = door2DownEffectPos.d[0];
    long long v0b = door2UpEffectPos.d[0];
    int i;
    for (i = 0; i < 50; i++) {
        switch (i) {
        case 0:
            b1[0] = v0a;
            b1[1] = door2DownEffectPos.d[1];
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

static ActMail door2_down_chk_mes[2] = {{430}, {429}}; /* derived name */

void actSt04dDoor2DownChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    long long buf[2];

    while (scpTriggerFloorAttrTargetMan(self, 0x2000000) != 0) {
        _ACTWait(1);
    }
    _ACTWait(15);

    actCreateSubThread(actSt04dDoor2DownEffect, 21);

    stage_SetAnimation(PAL_JPN(257, 253), 1, 0);

    buf[0] = door2SoundPos.d[0];
    buf[1] = door2SoundPos.d[1];
    soundSeDefPlay(1220, 0, (float *)buf, 1);
    _ACTWait(30);
    soundSeDefPlay(1221, 0, (float *)buf, 1);
    _ACTWait(30);
    soundSeDefPlay(1222, 0, (float *)buf, 1);

    while (stage_CheckAnimationFinish(PAL_JPN(257, 253)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    door2_down_chk_mes[0].func = actSt04dDoor2UpChk;
    sub->mail = door2_down_chk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}
