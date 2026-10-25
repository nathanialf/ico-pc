#include "st01b.h"
#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "act.h"
#include "gobj_process.h"
#include "commonact.h"
#include "way_llf.h"
#include "chain.h"
#include "generator.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "typedef.h"
#include "main.h"
#include "script.h"

/* This stage's actor mail records. Word 0 of each entry is the mail id the
   entry answers (430 = the actor's own wake-up post, 429 = the trailing
   entry); the handler in .func is installed at run time just before the
   record is posted. Each record is named for the actor thread that owns
   and posts it. */
static ActMail ene_mes[2] = {{430}, {429}}; /* derived name */

static ActMail floor_mes[2] = {{430}, {429}}; /* derived name */

static ActMail way_mes[2] = {{430}, {429}}; /* derived name */

static ActMail way_on_mes[2] = {{430}, {429}}; /* derived name */

static ActMail way_off_mes[2] = {{430}, {429}}; /* derived name */

void actSt01bInit(void)
{
    if (gflagChk(PAL_NTSCAUG6(70, 66)) == 0) {
        SetWayGroupActive(2, 0);
        if (ICO_NTSC) {
            stage_SetAnimation(162, 0, 0);
            return stage_SetAnimation(159, 0, 0);
        } else if (ICO_AUG6) {
            stage_SetAnimation(161, 0, 0);
            return stage_SetAnimation(158, 0, 0);
        } else {
            stage_SetAnimation(PAL_JPNKORCN(183, 179), 0, 0);
            return stage_SetAnimation(PAL_JPNKORCN(180, 176), 0, 0);
        }
    }
    SetWayGroupActive(2, 1);
    if (ICO_NTSC) {
        stage_SetAnimation(162, 0, -1);
        return stage_SetAnimation(159, 0, -1);
    } else if (ICO_AUG6) {
        stage_SetAnimation(161, 0, -1);
        return stage_SetAnimation(158, 0, -1);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(183, 179), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(180, 176), 0, -1);
        return FinishHint(9);
    }
}

void actSt01bEneChk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (gflagChk(PAL_NTSCAUG6(70, 66)) == 0 || scpTriggerFloorAttr(boyGObj, 0x1000000) == 0 ||
           (scpTriggerFloorAttr(girlGObj, 0x1000000) == 0 &&
            scpTriggerFloorAttr(girlGObj, 0x2000000) == 0)) {
        _ACTWait(1);
    }
    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6(55, 52, 51));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyOne(PAL_JPNKORCN_NTSC_AUG6(3757, 3734, 3453, 3415));
    _ACTWait(30);
    gflagOn(PAL_NTSCAUG6(68, 64));
    gflagOn(PAL_NTSCAUG6(69, 65));
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(182, 178, 161, 160), 1, 0);
    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6(182, 178, 161, 160), 90, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6(182, 178, 161, 160)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6(54, 51, 50));
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyOne(PAL_JPNKORCN_NTSC_AUG6(3757, 3734, 3453, 3415));
}

/* .sdata */
SqEntry *st01b_floor = 0;

unsigned int st01b_yure = 0;

unsigned char st01b_yure_vol = 0;

/* .sbss: the demo's own end flag, raised by the subthread the wait loop below
   spins for, and the handle of the looping sound effect the demo starts. */
static int demoEnd;

static int seHandle; /* derived name */

/* A 16-byte constant vector template: the float view carries the values,
   the long long view is the one the copy reads. */

static const ConstVec floorChkSubPos = {{-101.0f, -381.0f, -398.0f, 0.0f}}; /* derived name */

static void actSt01bFloorChkSub(GObj *volatile self)
{
    ConstVec pos;

    while (st01b_floor == 0) {
        _ACTWait(1);
    }
    AdpcmPlay(st01b_floor->stream);
    stage_SetAnimation(PAL_JPNKORCN(180, 176), 1, 0);
    stage_SetAnimation(PAL_JPNKORCN(181, 177), 1, 0);
    pos = floorChkSubPos;
    seHandle = soundSeDefPlay(1325, 0, pos.f, 1);
    _ACTWait(90);
    soundSeDefStop(seHandle);
    seHandle = -1;
    soundSeDefPlay(1288, 0, 0, 1);
    stage_SetAnimation(PAL_JPNKORCN(183, 179), 1, 0);
    st01b_yure = iosPadActRequest(boyPad, 9);
    st01b_yure_vol = 128;
    iosPadActVolumeSet(st01b_yure, 128);
    while (stage_CheckAnimationFrame(PAL_JPNKORCN(183, 179), 180, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

static void actSt01bFloorChk_ntsc(GObj *volatile self)
{
    ConstVec pos;
    int se;

    while (scpIsHangChainOptional(boyGObj, 747) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();
    gflagOn(66);
    SetWayGroupActive(2, 1);
    if (girlGObj != 0) {
        scpPlayPosSet(girlGObj, -200.0f, 900.0f, -200.0f);
    }
    _ACTWait(60);
    scpAdpcmPlayRequestFunc(79, &st01b_floor, 1, 1, 1);
    while (st01b_floor == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(159, 1, 0);
    stage_SetAnimation(160, 1, 0);
    pos = floorChkSubPos;
    se = soundSeDefPlay(1301, 0, pos.f, 1);
    _ACTWait(90);
    soundSeDefStop(se);
    soundSeDefPlay(1264, 0, 0, 1);
    stage_SetAnimation(162, 1, 0);
    st01b_yure = iosPadActRequest(boyPad, 9);
    st01b_yure_vol = 128;
    iosPadActVolumeSet(st01b_yure, 128);
    while (stage_CheckAnimationFrame(162, 180, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActStop(st01b_yure);
    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static void actSt01bFloorChk_aug6(GObj *volatile self)
{
    ConstVec pos;
    int se;

    while (scpIsHangChainOptional(boyGObj, 746) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();
    gflagOn(66);
    SetWayGroupActive(2, 1);
    if (girlGObj != 0) {
        scpPlayPosSet(girlGObj, -200.0f, 900.0f, -200.0f);
    }
    _ACTWait(60);
    scpAdpcmPlayRequestFunc(79, &st01b_floor, 1, 1, 1);
    while (st01b_floor == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(158, 1, 0);
    stage_SetAnimation(159, 1, 0);
    pos = floorChkSubPos;
    se = soundSeDefPlay(1301, 0, pos.f, 1);
    _ACTWait(90);
    soundSeDefStop(se);
    soundSeDefPlay(1264, 0, 0, 1);
    stage_SetAnimation(161, 1, 0);
    st01b_yure = iosPadActRequest(boyPad, 9);
    st01b_yure_vol = 128;
    iosPadActVolumeSet(st01b_yure, 128);
    while (stage_CheckAnimationFrame(161, 180, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActStop(st01b_yure);
    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actSt01bFloorChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt01bFloorChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt01bFloorChk_aug6(self);
        return;
    }
    GProc *th;
    int notdone;

    while (scpIsHangChainOptional(boyGObj, 805) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();
    gflagOn(70);
    FinishHint(9);
    SetWayGroupActive(2, 1);
    if (girlGObj != 0) {
        scpPlayPosSet(girlGObj, -200.0f, 900.0f, -200.0f);
    }
    scpAdpcmPlayRequestFunc(81, &st01b_floor, 1, 1, 0);
    th = actCreateSubThread(actSt01bFloorChkSub, 21);
    seHandle = -1;
    st01b_yure = 0xFFFFFFFF;
    demoEnd = 0;
    st01b_floor = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    notdone = demoEnd ^ 1;
    if (notdone) {
        while (st01b_floor == 0) {
            _ACTWait(1);
        }
        scpAdpcmFadeCloseFunc(&st01b_floor, 256);
        scpFadeOut(16.0f, 0, 0, 0);
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    }

    iosThreadSetPri(&th->thread, 34);

    if (notdone) {
        stage_SetAnimation(PAL_JPNKORCN(181, 177), 1, -1);
        stage_SetAnimation(PAL_JPNKORCN(180, 176), 1, -1);
        stage_SetAnimation(PAL_JPNKORCN(183, 179), 0, 180);
        if (seHandle >= 0) {
            soundSeDefStop(seHandle);
            soundSeDefPlay(1288, 0, 0, 1);
        }
        _ACTWait(1);
        ChainPositionReset(scpSearchGobj(805));
        _ACTWait(1);
        scpFadeIn(3.0f);
    }

    iosPadActStop(st01b_yure);
    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

void actSt01bSekizo(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    scpSekizou(self, PAL_NTSCAUG6(65, 61), PAL_JPNKORCN_NTSC_AUG6(178, 174, 157, 156), 0, 18,
               1000.0f, 528.0f, -150.0f, 1000.0f, 528.0f, -100.0f);
}

static void actSt01bEne_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(64) == 0) {
        static ActMail ene_mes[2] = {{397}, {396}}; /* derived name */

        ene_mes[0].func = actSt01bEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actSt01bEne_aug6(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(64) == 0) {
        static ActMail ene_mes[2] = {{393}, {392}}; /* derived name */

        ene_mes[0].func = actSt01bEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt01bEne(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt01bEne_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt01bEne_aug6(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(68) == 0) {
        ene_mes[0].func = actSt01bEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt01bEnemy1(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6(69, 65)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(116);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_MaskOff(self);
}

void actSt01bEnemy2(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6(69, 65)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(100);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_MaskOff(self);
}

void actSt01bEnemy3(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6(69, 65)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(130);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_MaskOff(self);
}

void actSt01bEnemy4(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6(69, 65)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(115);
    Generator_Call(self);
}

void actSt01bEnemy5(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6(69, 65)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(125);
    Generator_Call(self);
}

void actSt01bEnemy6(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6(69, 65)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(110);
    Generator_Call(self);
}

static void actSt01bFloor_ntsc(GObj *volatile self)
{
    GObj *x = self;
    static ActMail floor_mes[2] = {{397}, {396}}; /* derived name */
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(66) == 0) {
        floor_mes[0].func = actSt01bFloorChk;
        act->mail = floor_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actSt01bFloor_aug6(GObj *volatile self)
{
    GObj *x = self;
    static ActMail floor_mes[2] = {{393}, {392}}; /* derived name */
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(66) == 0) {
        floor_mes[0].func = actSt01bFloorChk;
        act->mail = floor_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt01bFloor(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt01bFloor_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt01bFloor_aug6(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(70) == 0) {
        floor_mes[0].func = actSt01bFloorChk;
        act->mail = floor_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static void actSt01bWay_ntsc(GObj *volatile self)
{
    GObj *x = self;
    static ActMail way_mes[2] = {{397}, {396}}; /* derived name */
    Act *act = actInitialize(self);

    _ACTWait(1);

    way_mes[0].func = actSt01bWayOnChk;
    act->mail = way_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static void actSt01bWay_aug6(GObj *volatile self)
{
    GObj *x = self;
    static ActMail way_mes[2] = {{393}, {392}}; /* derived name */
    Act *act = actInitialize(self);

    _ACTWait(1);

    way_mes[0].func = actSt01bWayOnChk;
    act->mail = way_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt01bWay(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt01bWay_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt01bWay_aug6(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    way_mes[0].func = actSt01bWayOnChk;
    act->mail = way_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt01bEnd(void) {}

void actSt01bSekizoEvent(int x)
{
    volatile int local = x;
}

void actSt01bFloorEvent(int x)
{
    volatile int local = x;
}

static void actSt01bWayOnChk_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    static ActMail way_on_mes[2] = {{397}, {396}}; /* derived name */

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerFloorAttr(girlGObj, 0x1000000) == 0 || gflagChk(66) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(4, 1);

    way_on_mes[0].func = actSt01bWayOffChk;
    sub->mail = way_on_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static void actSt01bWayOnChk_aug6(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    static ActMail way_on_mes[2] = {{393}, {392}}; /* derived name */

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerFloorAttr(girlGObj, 0x1000000) == 0 || gflagChk(66) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(4, 1);

    way_on_mes[0].func = actSt01bWayOffChk;
    sub->mail = way_on_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt01bWayOnChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt01bWayOnChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt01bWayOnChk_aug6(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerFloorAttr(girlGObj, 0x1000000) == 0 || gflagChk(70) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(4, 1);

    way_on_mes[0].func = actSt01bWayOffChk;
    sub->mail = way_on_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt01bWayOffChk_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    static ActMail way_off_mes[2] = {{397}, {396}}; /* derived name */

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerFloorAttr(girlGObj, 0x2000000) == 0 || gflagChk(66) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(4, 0);

    way_off_mes[0].func = actSt01bWayOnChk;
    sub->mail = way_off_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static void actSt01bWayOffChk_aug6(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    static ActMail way_off_mes[2] = {{393}, {392}}; /* derived name */

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerFloorAttr(girlGObj, 0x2000000) == 0 || gflagChk(66) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(4, 0);

    way_off_mes[0].func = actSt01bWayOnChk;
    sub->mail = way_off_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt01bWayOffChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt01bWayOffChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt01bWayOffChk_aug6(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerFloorAttr(girlGObj, 0x2000000) == 0 || gflagChk(70) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(4, 0);

    way_off_mes[0].func = actSt01bWayOnChk;
    sub->mail = way_off_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}
