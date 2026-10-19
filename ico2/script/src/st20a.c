#include "st20a.h"
#include "gamesys.h"
#include "debug.h"
#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "act.h"
#include "gobj_process.h"
#include "boyact.h"
#include "commonact.h"
#include "girl_act.h"
#include "way_llf.h"
#include "generator.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "script.h"
#include "StageAnimation.h"
#include "typedef.h"
#include "main.h"

static void actSt20aBridgeDownSub(GObj *volatile self);
static void actSt20aGirlPosChk(GObj *volatile self);
static void actSt20aHint1Chk(GObj *volatile self);

/* This stage's actor mail records. Word 0 of each entry is the mail id the
   entry answers (430 = the actor's own wake-up post, 429 = the trailing
   entry); the handler in .func is installed at run time just before the
   record is posted. The two main-thread records answer their own ids and
   carry their switch handler from the start. Each record is named for the
   actor thread that owns and posts it. */
static ActMail bridgeMain_mes[2] = {{406, actSt20aBridgeSwitch}, {429}}; /* derived name */

static ActMail bridge_mes[2] = {{430}, {429}}; /* derived name */

static ActMail bridgeSwitch_mes[2] = {{430}, {429}}; /* derived name */

static ActMail gondolaMain_mes[2] = {{407, actSt20aGondolaSwitch}, {429}}; /* derived name */

static ActMail gondola_mes[2] = {{430}, {429}}; /* derived name */

static ActMail gondolaSwitchUp_mes[2] = {{430}, {429}}; /* derived name */

static ActMail gondolaSwitchDown_mes[2] = {{430}, {429}}; /* derived name */

static ActMail gondolaDown_mes[2] = {{430}, {429}}; /* derived name */

static ActMail gondolaUp_mes[2] = {{430}, {429}}; /* derived name */

static ActMail exit_mes[2] = {{430}, {429}}; /* derived name */

static ActMail ene_mes[2] = {{430}, {429}}; /* derived name */

static ActMail fence_mes[2] = {{430}, {429}}; /* derived name */

static ActMail fence2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail fenceDownChk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail fenceUpChk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail fenceDownChk2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail fenceUpChk2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail girlPos_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hint1_mes[2] = {{430}, {429}}; /* derived name */

void actSt20aInit(void)
{
    if (gflagChk(315) == 0) {
        stage_SetAnimation(PAL_JPNKOR(148, 144), 0, 0);
        SetWayGroupActive(4, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(148, 144), 0, -1);
        SetWayGroupActive(4, 1);
    }
}

void actSt20aEnd(void)
{
    if (girlGObj != 0 && gflagChk(315) != 0 && gflagChk(318) == 0) {
        gflagOn(391);
    }
    gamesysObjInfoCls(scpSearchGobj(2025)->kind, scpSearchGobj(2025)->labelId);
    gamesysObjInfoCls(scpSearchGobj(2023)->kind, scpSearchGobj(2023)->labelId);
}

/* .sbss: the demo's own end flag, raised by the subthread the wait loop below
   spins for. */
static int demoEnd;

/* .sdata: the bridge and gondola stream handles and the shake. */
SqEntry *brg20a = 0;

SqEntry *gondola_up = 0;

SqEntry *gondola_down = 0;

unsigned int st20a_yure = 0;

unsigned char st20a_yure_vol = 0;

void actSt20aBridgeDown(GObj *volatile self)
{
    GProc *th;

    lt_switch_layout(PAL_JPNKOR(55, 52));
    scpSleepEnemyAll();
    gflagOn(315);
    gflagOff(391);
    st20a_yure = 0xFFFFFFFF;
    demoEnd = 0;
    scpAdpcmPlayRequestFunc(71, &brg20a, 1, 1, 0);
    th = actCreateSubThread(actSt20aBridgeDownSub, 21);
    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }
    iosThreadSetPri(&th->thread, 34);
    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);
        while (brg20a == 0) {
            _ACTWait(1);
        }
        scpAdpcmFadeCloseFunc(&brg20a, 512);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }
        stage_SetAnimation(PAL_JPNKOR(148, 144), 0, -1);
        scpFadeIn(3.0f);
    }
    iosPadActStop(st20a_yure);
    SetWayGroupActive(4, 1);
    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKOR(54, 51));
}

void actSt20aGondolaDown(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    SetGirlDangerGObj(boyGObj);
    scpAdpcmPlayRequestFunc(69, &gondola_down, 1, 1, 1);
    while (gondola_down == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(PAL_JPNKOR(147, 143), 1, 0);
    gflagOn(316);
    while (stage_CheckAnimationFrame(PAL_JPNKOR(147, 143), 150, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 17);
    while (stage_CheckAnimationFrame(PAL_JPNKOR(147, 143), 500, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 16);
    if (gondola_down != 0) {
        scpAdpcmFadeCloseFunc(&gondola_down, 256);
    }
    while (scpAdpcmCloseChkFunc(&gondola_down) != 0) {
        _ACTWait(1);
    }
    ClearGirlDangerGObj();
    gondolaDown_mes[0].func = actSt20aGondolaMain;
    sub->mail = gondolaDown_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt20aGondolaUp(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpAdpcmPlayRequestFunc(70, &gondola_up, 1, 1, 1);
    while (gondola_up == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(PAL_JPNKOR(147, 143), 1, 500);
    gflagOff(316);
    while (stage_CheckAnimationFrame(PAL_JPNKOR(147, 143), 820, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 17);
    while (stage_CheckAnimationFrame(PAL_JPNKOR(147, 143), 1000, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 16);
    if (gondola_up != 0) {
        scpAdpcmFadeCloseFunc(&gondola_up, 256);
    }
    while (scpAdpcmCloseChkFunc(&gondola_up) != 0) {
        _ACTWait(1);
    }
    gondolaUp_mes[0].func = actSt20aGondolaMain;
    sub->mail = gondolaUp_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt20aFence(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);
    if (gflagChk(309) == 0) {
        SetWayGroupActive(19, 1);

        scpSearchGobj(2024)->active = 0;
        scpSearchGobj(2025)->active = 0;

        scpSearchGobj(2030)->active = 0;
        scpSearchGobj(2031)->active = 0;
        scpSearchGobj(2032)->active = 0;
        scpSearchGobj(2033)->active = 0;

        scpSearchGobj(2026)->active = 0;
        scpSearchGobj(2027)->active = 0;
        scpSearchGobj(2028)->active = 0;
        scpSearchGobj(2029)->active = 0;

        scpLinkBGAtoLayoutedTarget(2022, PAL_JPNKOR(149, 145));
        stage_SetAnimation(PAL_JPNKOR(149, 145), 0, 30);

        fence_mes[0].func = actSt20aFenceUpChk;
        sub->mail = fence_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        SetWayGroupActive(19, 0);

        scpSearchGobj(2022)->active = 0;
        scpSearchGobj(2023)->active = 0;

        scpSearchGobj(2026)->active = 0;
        scpSearchGobj(2027)->active = 0;
        scpSearchGobj(2028)->active = 0;
        scpSearchGobj(2029)->active = 0;

        gflagOff(309);

        scpLinkBGAtoLayoutedTarget(2024, PAL_JPNKOR(149, 145));
        stage_SetAnimation(PAL_JPNKOR(149, 145), 0, 0);

        if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x2000000) != 0) {
            scpPlayPosSet(girlGObj, 3973.0f, -1100.0f, -1169.0f);
        }

        fence2_mes[0].func = actSt20aFenceDownChk2;
        sub->mail = fence2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt20aFenceDownChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerBall(self, scpSearchGobj(2022), 5.0f) == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(PAL_JPNKOR(149, 145), 1, 0);
    while (stage_CheckAnimationFrame(PAL_JPNKOR(149, 145), 10, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    soundSeDefPlay(1339, 0, 0, 1);
    while (stage_CheckAnimationFrame(PAL_JPNKOR(149, 145), 30, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    SetWayGroupActive(19, 1);
    scpSearchGobj(2026)->active = 0;
    scpSearchGobj(2027)->active = 0;
    scpSearchGobj(2028)->active = 0;
    scpSearchGobj(2029)->active = 0;
    gflagOff(320);
    fenceDownChk_mes[0].func = actSt20aFenceUpChk;
    sub->mail = fenceDownChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt20aFenceUpChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerBall(self, scpSearchGobj(2022), 5.0f) != 0) {
        _ACTWait(1);
    }
    SetWayGroupActive(19, 0);
    stage_SetAnimation(PAL_JPNKOR(149, 145), 1, 31);
    while (stage_CheckAnimationFrame(PAL_JPNKOR(149, 145), 40, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    soundSeDefPlay(1339, 0, 0, 1);
    while (stage_CheckAnimationFinish(PAL_JPNKOR(149, 145)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpSearchGobj(2026)->active = 1;
    scpSearchGobj(2027)->active = 1;
    scpSearchGobj(2028)->active = 1;
    scpSearchGobj(2029)->active = 1;
    gflagOn(320);
    fenceUpChk_mes[0].func = actSt20aFenceDownChk;
    sub->mail = fenceUpChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt20aFenceDownChk2(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerBall(self, scpSearchGobj(2024), 5.0f) == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(PAL_JPNKOR(149, 145), 1, 0);
    while (stage_CheckAnimationFrame(PAL_JPNKOR(149, 145), 10, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    soundSeDefPlay(1339, 0, 0, 1);
    while (stage_CheckAnimationFrame(PAL_JPNKOR(149, 145), 30, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpSearchGobj(2030)->active = 0;
    scpSearchGobj(2031)->active = 0;
    scpSearchGobj(2032)->active = 0;
    scpSearchGobj(2033)->active = 0;
    SetWayGroupActive(19, 1);
    gflagOff(320);
    fenceDownChk2_mes[0].func = actSt20aFenceUpChk2;
    sub->mail = fenceDownChk2_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt20aFenceUpChk2(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerBall(self, scpSearchGobj(2024), 5.0f) != 0) {
        _ACTWait(1);
    }
    SetWayGroupActive(19, 0);
    stage_SetAnimation(PAL_JPNKOR(149, 145), 1, 31);
    while (stage_CheckAnimationFrame(PAL_JPNKOR(149, 145), 40, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    soundSeDefPlay(1339, 0, 0, 1);
    while (stage_CheckAnimationFinish(PAL_JPNKOR(149, 145)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpSearchGobj(2030)->active = 1;
    scpSearchGobj(2031)->active = 1;
    scpSearchGobj(2032)->active = 1;
    scpSearchGobj(2033)->active = 1;
    gflagOn(320);
    fenceUpChk2_mes[0].func = actSt20aFenceDownChk2;
    sub->mail = fenceUpChk2_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt20aBridge(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);
    if (gflagChk(315) == 0) {
        bridge_mes[0].func = actSt20aBridgeMain;
        sub->mail = bridge_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt20aGondola(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);
    if (gflagChk(316) != 0) {
        stage_SetAnimation(PAL_JPNKOR(147, 143), 0, 0);
        _ACTWait(10);
        stage_SetAnimation(PAL_JPNKOR(147, 143), 0, 500);
    } else {
        stage_SetAnimation(PAL_JPNKOR(147, 143), 0, 0);
    }
    gondola_mes[0].func = actSt20aGondolaMain;
    sub->mail = gondola_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt20aExit(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);
    exit_mes[0].func = actSt20aExitChk;
    sub->mail = exit_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt20aElv(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    if (gflagChk(309) != 0) {
        scpSearchGobj(2022)->active = 0;
        scpSearchGobj(2023)->active = 0;
        gflagOff(309);
    } else {
        scpSearchGobj(2024)->active = 0;
        scpSearchGobj(2025)->active = 0;
    }
}

void actSt20aEne(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);
    if (gflagChk(318) == 0) {
        ene_mes[0].func = actSt20aEneChk;
        sub->mail = ene_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt20aEnemy1(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(319) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    Generator_MaskOff(self);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt20aEnemy2(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(319) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    Generator_MaskOff(self);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt20aEnemy3(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(319) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    Generator_MaskOff(self);
}

void actSt20aHint1(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);
    if (gflagChk(321) == 0) {
        hint1_mes[0].func = actSt20aHint1Chk;
        sub->mail = hint1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        FinishHint(20);
    }
}

void actSt20aGirlPos(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);
    if (gflagChk(322) == 0) {
        SleepHint(20);
        girlPos_mes[0].func = actSt20aGirlPosChk;
        sub->mail = girlPos_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

/* the actor entry's parameter is its frame home: the thread switch writes it */
void actSt20aBridgeMain(GObj *volatile self)
{
    GOBJ_ACT(self)->mainMail = bridgeMain_mes;
    scpBoyControlReadDisable = 0;
    while (1) {
        _ACTWait(1);
    }
}

void actSt20aBridgeSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;
    bridgeSwitch_mes[0].func = actSt20aBridgeDown;
    sub->mainMail = 0;
    sub->mail = bridgeSwitch_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt20aBridgeDownSub(GObj *volatile self)
{
    _ACTWait(30);
    while (brg20a == 0) {
        _ACTWait(1);
    }
    AdpcmPlay(brg20a->stream);
    stage_SetAnimation(PAL_JPNKOR(148, 144), 1, 0);
    st20a_yure = iosPadActRequest(boyPad, 9);
    st20a_yure_vol = 128;
    iosPadActVolumeSet(st20a_yure, 128);
    while (stage_CheckAnimationFinish(PAL_JPNKOR(148, 144)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

void actSt20aGondolaMain(GObj *volatile self)
{
    Act *p = GOBJ_ACT(self);

    lt_switch_layout(PAL_JPNKOR(54, 51));
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
    p->mainMail = gondolaMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt20aGondolaSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    lt_switch_layout(PAL_JPNKOR(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();
    if (gflagChk(316) != 0) {
        gondolaSwitchUp_mes[0].func = actSt20aGondolaUp;
        sub->mail = gondolaSwitchUp_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
    gondolaSwitchDown_mes[0].func = actSt20aGondolaDown;
    sub->mail = gondolaSwitchDown_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt20aExitChk(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 400.0f) == 0 ||
           scpTriggerFloorAttr(boyGObj, 0x2000000) == 0) {
        _ACTWait(1);
    }
    gflagOn(317);
    gflagOff(309);
    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x2000000) != 0) {
        OnGirlEscortFlag();
        RequestStageChange(4, boyGObj, girlGObj, 2.0f, 8.0f);
    }
    RequestStageChange(4, boyGObj, 0, 2.0f, 8.0f);
}

void actSt20aEneChk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (gflagChk(315) == 0 || scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    gflagOn(318);
    gflagOn(319);
}

static void actSt20aGirlPosChk(GObj *volatile self)
{
    while (girlGObj == 0 || scpTriggerBall(self, girlGObj, 200.0f) == 0) {
        _ACTWait(1);
    }
    gflagOn(322);
    WakeupHint(20);
}

static void actSt20aHint1Chk(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 100.0f) == 0 &&
           scpTriggerFloorAttr(boyGObj, 0x4000000) == 0) {
        _ACTWait(1);
    }
    debug_StdPrintfDummy("HINT1_FINISH!!!!!!!!!!!!!!!\n");
    gflagOn(321);
    FinishHint(20);
}
