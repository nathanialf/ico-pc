#include "st09a.h"
#include "debug.h"
#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
#include "s_init.h"
#include "act.h"
#include "gobj_process.h"
#include "commonact.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "script.h"
#include "StageAnimation.h"
#include "motionManager2.h"
#include "typedef.h"
#include "main.h"

static void actSt09aBrgDownSub(GObj *volatile self);
static void actSt09aHint1Chk(GObj *volatile self);
static void actSt09aHint2Chk(GObj *volatile self);

/* .sbss: the demo's own end flag, raised by the subthread the wait loop below
   spins for. */
static int demoEnd;

/* .data: actor mail packets. */

static ActMail elvMain_mes[2] = {{406, actSt09aElvSwitch}, {429}}; /* derived name */

static ActMail elv_mes[2] = {{430}, {429}}; /* derived name */

static ActMail elvSwitchUp_mes[2] = {{430}, {429}}; /* derived name */

static ActMail elvSwitchDown_mes[2] = {{430}, {429}}; /* derived name */

static ActMail elvDown_mes[2] = {{430}, {429}}; /* derived name */

static ActMail elvUp_mes[2] = {{430}, {429}}; /* derived name */

static float sekizoPos[4] = {1548.0f, -412.0f, -608.0f, 0.0f}; /* derived name */

static ActMail intro_mes[2] = {{430}, {429}}; /* derived name */

static ActMail brgMain_mes[2] = {{407, actSt09aBrgSwitch}, {429}}; /* derived name */

static ActMail brg_mes[2] = {{430}, {429}}; /* derived name */

static ActMail brgSwitch_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hint1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hint2_mes[2] = {{430}, {429}}; /* derived name */

/* .sdata: the bridge stream handle. */
SqEntry *st09a_brg = 0;

void actSt09aInit(void)
{
    if (gflagChk(86) == 0) {
        stage_SetAnimation(PAL_JPN(378, 374), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPN(378, 374), 0, -1);
    }
}

void actSt09aElvDown(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);
    /* the sound handle, which the sound subsystem owns; it is kept across
       the animation wait and read again for soundSeDefStop */
    volatile int se;

    lt_switch_layout(PAL_JPN(55, 52));
    LockForceGroundParent(boyGObj);

    stage_SetAnimation(PAL_JPN(375, 371), 1, 0);

    se = soundSeDefPlay(1217, 0, 0, 1);

    gflagOn(83);

    while (stage_CheckAnimationFrame(PAL_JPN(375, 371), 210, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    soundSeDefStop(se);

    UnlockForceGroundParent(boyGObj);

    lt_switch_layout(PAL_JPN(54, 51));

    elvDown_mes[0].func = actSt09aElvMain;
    act->mail = elvDown_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt09aBrgDown(GObj *volatile self)
{
    GProc *th;

    lt_switch_layout(PAL_JPN(55, 52));
    gflagOn(86);
    scpAdpcmPlayRequestFunc(90, &st09a_brg, 1, 1, 1);

    while (st09a_brg == 0) {
        _ACTWait(1);
    }

    th = actCreateSubThread(actSt09aBrgDownSub, 21);
    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&th->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);
        scpAdpcmFadeCloseFunc(&st09a_brg, 256);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }

        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(PAL_JPN(378, 374), 0, -1);
        scpFadeIn(5.0f);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    }
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPN(54, 51));
}

void actSt09aElv(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(83) != 0) {
        stage_SetAnimation(PAL_JPN(375, 371), 0, 210);
    } else {
        stage_SetAnimation(PAL_JPN(375, 371), 0, 0);
    }

    elv_mes[0].func = actSt09aElvMain;
    act->mail = elv_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt09aSekizo(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    soundSeDefPlay(1346, 0, sekizoPos, 1);
    soundSeDefPlay(1347, 0, sekizoPos, 1);
    soundSeDefPlay(1348, 0, sekizoPos, 1);

    scpSekizou(self, 84, PAL_JPN(376, 372), 0, 18, -1350.0f, -100.0f, 1515.0f, -1450.0f, -100.0f,
               1515.0f);
}

void actSt09aIntro(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(85) == 0) {
        intro_mes[0].func = actSt09aIntroChk;
        act->mail = intro_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt09aBrg(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(86) == 0) {
        brg_mes[0].func = actSt09aBrgMain;
        act->mail = brg_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt09aHint1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(87) == 0) {
        hint1_mes[0].func = actSt09aHint1Chk;
        act->mail = hint1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        FinishHint(10);
    }
}

void actSt09aHint2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(88) == 0) {
        hint2_mes[0].func = actSt09aHint2Chk;
        act->mail = hint2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        FinishHint(11);
    }
}

void actSt09aElvMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 0;

    sub->mainMail = elvMain_mes;

    while (1) {
        _ACTWait(1);
    }
}

void actSt09aElvSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    if (gflagChk(83) != 0) {
        elvSwitchUp_mes[0].func = actSt09aElvUp;
        sub->mail = elvSwitchUp_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }

    elvSwitchDown_mes[0].func = actSt09aElvDown;
    sub->mail = elvSwitchDown_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt09aElvUp(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);
    /* the sound handle, which the sound subsystem owns; it is kept across
       the animation wait and read again for soundSeDefStop */
    volatile int se;

    lt_switch_layout(PAL_JPN(55, 52));

    stage_SetAnimation(PAL_JPN(375, 371), 1, 211);

    se = soundSeDefPlay(1217, 0, 0, 1);

    gflagOff(83);

    while (stage_CheckAnimationFrame(PAL_JPN(375, 371), 420, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    soundSeDefStop(se);

    lt_switch_layout(PAL_JPN(54, 51));

    elvUp_mes[0].func = actSt09aElvMain;
    act->mail = elvUp_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt09aSekizoEvent(int x)
{
    volatile int local = x;
}

void actSt09aIntroChk(GObj *volatile self)
{
    lt_switch_layout(PAL_JPN(55, 52));
    gflagOn(85);

    scpBoyControlReadDisable = 1;
    _ACTWait(1);

    stage_SetAnimation(PAL_JPN(377, 373), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPN(377, 373)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(PAL_JPN(54, 51));

    scpBoyControlReadDisable = 0;
}

void actSt09aBrgMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = brgMain_mes;

    while (1) {
        _ACTWait(1);
    }
}

void actSt09aBrgSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    brgSwitch_mes[0].func = actSt09aBrgDown;
    sub->mail = brgSwitch_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt09aBrgDownSub(GObj *volatile self)
{
    stage_SetAnimation(PAL_JPN(378, 374), 1, 0);

    while (stage_CheckAnimationFrame(PAL_JPN(378, 374), 115, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 17);

    while (stage_CheckAnimationFrame(PAL_JPN(378, 374), 165, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFinish(PAL_JPN(378, 374)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

static void actSt09aHint1Chk(GObj *volatile self)
{
    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    debug_StdPrintfDummy("HINT1_FINISH!!!!!!!!!!!!!!!\n");

    gflagOn(87);
    FinishHint(10);
}

static void actSt09aHint2Chk(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 400.0f) == 0) {
        _ACTWait(1);
    }

    debug_StdPrintfDummy("HINT2_FINISH!!!!!!!!!!!!!!!\n");

    gflagOn(88);
    FinishHint(11);
}
