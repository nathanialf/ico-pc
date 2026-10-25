#include "s_init.h"
#include "act.h"
#include "commonact.h"
#include "generator.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "script.h"
#include "StageAnimation.h"
#include "geometryManager.h"
#include "item.h"
#include "quaternion.h"
#include "typedef.h"
#include "main.h"

/* A 16-byte constant vector template: the float view carries the values,
   the long long view is the one the copy reads. */

static const ConstVec door2UpChkPos = {{-702.0f, -1886.0f, -5680.0f, 0.0f}}; /* derived name */

static const ConstVec door2UpEffectPos = {{-704.0f, -1700.0f, -5679.0f, 1.0f}}; /* derived name */

static const ConstVec door2UpEffect2Pos = {{-704.0f, -1955.0f, -5679.0f, 1.0f}}; /* derived name */

void actSt05dDoor2UpEffect(GObj *volatile self);
void actSt05dDoor2DownEffect(GObj *volatile self);
void actSt05dEneChk(GObj *volatile self);
static void actSt05dCrestHintChk(GObj *volatile self);

static ActMail door2Down_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door2Down_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door2Down_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define door2Down_mes                                                                              \
    PAL_NTSC_AUG6JUN26(door2Down_mes_pal, door2Down_mes_ntsc, door2Down_mes_aug6jun26)

static ActMail door2Up_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door2Up_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door2Up_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define door2Up_mes PAL_NTSC_AUG6JUN26(door2Up_mes_pal, door2Up_mes_ntsc, door2Up_mes_aug6jun26)

static ActMail door2UpChk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door2UpChk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door2UpChk_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define door2UpChk_mes                                                                             \
    PAL_NTSC_AUG6JUN26(door2UpChk_mes_pal, door2UpChk_mes_ntsc, door2UpChk_mes_aug6jun26)

static ActMail door2DownChk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door2DownChk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door2DownChk_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define door2DownChk_mes                                                                           \
    PAL_NTSC_AUG6JUN26(door2DownChk_mes_pal, door2DownChk_mes_ntsc, door2DownChk_mes_aug6jun26)

static ActMail ene_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail ene_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail ene_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define ene_mes PAL_NTSC_AUG6JUN26(ene_mes_pal, ene_mes_ntsc, ene_mes_aug6jun26)

static ActMail crestHint_mes[2] = {{430}, {429}}; /* derived name */

void actSt05dDoor2DownChk(GObj *volatile self);
void actSt05dDoor2UpChk(GObj *volatile self);

void actSt05dDoor2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (scpTriggerBall(self, boyGObj, 400.0f) != 0 ||
        (girlGObj != 0 && scpTriggerBall(self, girlGObj, 400.0f) != 0)) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(349, 345, 316, 315), 0, 0);
        _ACTWait(60);
        door2Down_mes[0].func = actSt05dDoor2DownChk;
        act->mail = door2Down_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(348, 344, 315, 314), 0, 0);
        door2Up_mes[0].func = actSt05dDoor2UpChk;
        act->mail = door2Up_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt05dDoor2UpChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    long long buf[2];

    while (scpTriggerFloorAttrTargetMan(self, 0x2000000) == 0) {
        _ACTWait(1);
    }
    _ACTWait(15);

    actCreateSubThread(actSt05dDoor2UpEffect, 21);

    ReviveAllCarryableItemsWithNonSleepFrame(60);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(348, 344, 315, 314), 1, 0);

    buf[0] = door2UpChkPos.d[0];
    buf[1] = door2UpChkPos.d[1];
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1220, 1196), 0, (float *)buf, 1);
    _ACTWait(30);
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1221, 1197), 0, (float *)buf, 1);
    _ACTWait(30);
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1222, 1198), 0, (float *)buf, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(348, 344, 315, 314)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    door2UpChk_mes[0].func = actSt05dDoor2DownChk;
    sub->mail = door2UpChk_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

void actSt05dDoor2DownChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    long long buf[2];

    while (scpTriggerFloorAttrTargetMan(self, 0x2000000) != 0) {
        _ACTWait(1);
    }
    _ACTWait(15);

    actCreateSubThread(actSt05dDoor2DownEffect, 21);

    ReviveAllCarryableItemsWithNonSleepFrame(60);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(349, 345, 316, 315), 1, 0);

    buf[0] = door2UpChkPos.d[0];
    buf[1] = door2UpChkPos.d[1];
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1220, 1196), 0, (float *)buf, 1);
    _ACTWait(30);
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1221, 1197), 0, (float *)buf, 1);
    _ACTWait(30);
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1222, 1198), 0, (float *)buf, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(349, 345, 316, 315)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    door2DownChk_mes[0].func = actSt05dDoor2UpChk;
    sub->mail = door2DownChk_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

static void actSt05dEne_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(153) == 0) {
        ene_mes[0].func = actSt05dEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actSt05dEne_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(153) == 0) {
        ene_mes[0].func = actSt05dEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt05dEne(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt05dEne_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt05dEne_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    GObj *g;
    short rot;

    _ACTWait(1);

    g = scpSearchGobj(1458);
    rot = scpGetRotObjectCurrentRot(1556);

    GetRootQuaternion(GetCurrentQuaternion(), g);
    RotQuaternionY(GetCurrentQuaternion(), rot);
    SetRootQuaternion(g, GetCurrentQuaternion());
    UpdateRootMatrix(g);

    if (gflagChk(171) == 0) {
        ene_mes[0].func = actSt05dEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static void actSt05dEnemy1_ntsc(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(154) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    Generator_MaskOff(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

static void actSt05dEnemy1_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);

    while (gflagChk(154) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    Generator_MaskOff(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt05dEnemy1(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt05dEnemy1_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt05dEnemy1_aug6jun26(self);
        return;
    }
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    Generator_Mask(scpSearchGobj(1477));
    Generator_Mask(scpSearchGobj(1478));
    while (gflagChk(172) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    Generator_MaskOff(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_Call(scpSearchGobj(1477));
    Generator_Call(scpSearchGobj(1478));
}

void actSt05dEnemy2(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(PAL_NTSCAUG6JUN26(172, 154)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    Generator_MaskOff(self);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt05dCrestHint(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(173) == 0) {
        SleepHint(25);
        crestHint_mes[0].func = actSt05dCrestHintChk;
        act->mail = crestHint_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt05dDoor2Event(int x)
{
    volatile int local = x;
}

void actSt05dDoor2UpEffect(GObj *volatile self)
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

void actSt05dDoor2DownEffect(GObj *volatile self)
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

void actSt05dEneChk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (girlGObj == 0 || gflagChk(PAL_NTSCAUG6JUN26(170, 152)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    gflagOn(PAL_NTSCAUG6JUN26(171, 153));
    gflagOn(PAL_NTSCAUG6JUN26(172, 154));
}

static void actSt05dCrestHintChk(GObj *volatile self)
{
    while (gflagChk(243) == 0 || gflagChk(244) == 0 || gflagChk(245) == 0 || gflagChk(232) != 0) {
        _ACTWait(1);
    }
    gflagOn(173);
    WakeupHint(25);
}
