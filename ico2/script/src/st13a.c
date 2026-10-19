#include "st13a.h"
#include "StageManager.h"
#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "act.h"
#include "gobj_process.h"
#include "commonact.h"
#include "chain.h"
#include "gflag.h"
#include "script.h"
#include "StageAnimation.h"
#include <libvu0.h>
#include "typedef.h"
#include "main.h"

/* .sbss: the demo's own end flag, raised by the subthreads the wait loops
   below spin for. */
static int demoEnd;

static void actSt13aElevUpSub(GObj *volatile self);
static void actSt13aElevDownSub(GObj *volatile self);
void actSt13aElevDownChk(GObj *volatile self);

/* .data: actor mail packets. */

/* The chain-OK watcher's mail record: it installs actSt13aChainNG here and
   posts it to hand the chain back to the NG (hang-disabled) watcher. Word 0
   of each entry is the mail id the entry answers (430 the actor post,
   429 the trailing entry); .func is filled in at run time. Named for the
   thread that owns and posts it. */

static ActMail elevMain_mes[2] = {{406, actSt13aElevSwitch}, {429}}; /* derived name */

static ActMail elev_mes[2] = {{430}, {429}}; /* derived name */

static ActMail elevSwitch_mes[2] = {{430}, {429}}; /* derived name */

static ActMail elevDown_mes[2] = {{430}, {429}}; /* derived name */

static ActMail sekizo_mes[2] = {{430}, {429}}; /* derived name */

static ActMail check_mes[2] = {{430}, {429}}; /* derived name */

static ActMail chainNg_mes[2] = {{430}, {429}}; /* derived name */

static ActMail chainOk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail chainOK_mes[2] = {{430}, {429}}; /* derived name */

static ActMail chainNG_mes[2] = {{430}, {429}}; /* derived name */

/* .sdata: the lift and statue stream handles and shakes. */
SqEntry *st13a_up = 0;

SqEntry *st13a_down = 0;

SqEntry *sekizo13a = 0;

unsigned int st13a_yure = 0;

unsigned char st13a_yure_vol = 0;

int sekizo_13a = 0;

unsigned char sekizo_13a_vol = 0;

static void actSt13aElevUpSub(GObj *volatile self)
{
    AdpcmPlay(st13a_up->stream);

    stage_SetAnimation(PAL_JPNKOR(173, 169), 1, 0);
    stage_SetAnimation(PAL_JPNKOR(174, 170), 1, 0);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(173, 169), 86, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 17);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(173, 169), 140, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    st13a_yure = iosPadActRequest(boyPad, 9);
    st13a_yure_vol = 128;
    iosPadActVolumeSet(st13a_yure, 128);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(173, 169), 200, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    st13a_yure_vol = 64;

    while (stage_CheckAnimationFrame(PAL_JPNKOR(173, 169), 380, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

void actSt13aElevUp(GObj *volatile self)
{
    GProc *th;

    scpAdpcmPlayRequestFunc(77, &st13a_up, 0, 1, 0);
    while (st13a_up == 0) {
        _ACTWait(1);
    }

    preload(15);

    st13a_yure = -1;
    th = actCreateSubThread(actSt13aElevUpSub, 21);
    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&th->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);
        scpAdpcmFadeCloseFunc(&st13a_up, 512);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }
    }

    iosPadActStop(st13a_yure);
    gflagOn(325);
    RequestStageChange(15, boyGObj, 0, 0.025f, 8.0f);
}

void actSt13aElevDown(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(15) != 0) {
        scpFadeOut(255.0f, 0, 0, 0);
        stage_SetAnimation(PAL_JPNKOR(173, 169), 0, 0);
        lt_switch_layout(PAL_JPNKOR(55, 52));
        scpBoyControlReadDisable = 1;
        scpPlayStart(boyGObj);
        scpAdpcmPlayRequestFunc(80, &st13a_down, 1, 1, 0);
        _ACTWait(10);
        stage_SetAnimation(PAL_JPNKOR(173, 169), 0, 451);

        elevDown_mes[0].func = actSt13aElevDownChk;
        act->mail = elevDown_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static void actSt13aElevDownSub(GObj *volatile self)
{
    stage_SetAnimation(PAL_JPNKOR(173, 169), 1, 451);
    stage_SetAnimation(PAL_JPNKOR(175, 171), 1, 0);

    scpPlayPosSet(boyGObj, -4871.0f, -2800.0f, 2699.0f);

    st13a_yure = iosPadActRequest(boyPad, 9);
    st13a_yure_vol = 128;
    iosPadActVolumeSet(st13a_yure, 128);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(173, 169), 850, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 17);
    iosPadActStop(st13a_yure);
    st13a_yure = -1;

    while (stage_CheckAnimationFinish(PAL_JPNKOR(173, 169)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

void actSt13aElevDownChk(GObj *volatile self)
{
    GProc *th;

    while (st13a_down == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(st13a_down->stream);
    scpFadeIn(6.0f);

    th = actCreateSubThread(actSt13aElevDownSub, 21);
    demoEnd = 0;
    st13a_yure = -1;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&th->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);
        scpAdpcmFadeCloseFunc(&st13a_down, 512);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }
        stage_SetAnimation(PAL_JPNKOR(175, 171), 0, -1);
        stage_SetAnimation(PAL_JPNKOR(173, 169), 0, -1);
        _ACTWait(2);
        scpPlayPosSet(boyGObj, -4871.0f, 3527.0f, 2699.0f);
        iosPadActStop(st13a_yure);
        scpFadeIn(3.0f);
    }

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    lt_switch_layout(PAL_JPNKOR(54, 51));
    scpBoyControlReadDisable = 0;
    gflagOff(15);
}

void actSt13aSekizoChk(GObj *volatile self)
{
    float d[4];

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 || scpGameStat_BoyWeaponkind() != 5 ||
           scpActStatusDeathFall(boyGObj) != 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKOR(55, 52));
    scpBoyControlReadDisable = 1;

    scpAdpcmPlayRequestFunc(18, &sekizo13a, 1, 1, 1);
    while (sekizo13a == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JPNKOR(176, 172), 1, 0);

    sekizo_13a = iosPadActRequest(boyPad, 9);
    sekizo_13a_vol = 128;
    iosPadActVolumeSet(sekizo_13a, 128);

    scpPlayStart(boyGObj);
    scpPlayPosSet(boyGObj, -3688.0f, 3527.0f, 2502.0f);
    scpPlayMot(boyGObj, 0);
    _ACTWait(1);

    sceVu0SubVector(d, test_CURRENTROOT(self), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, d);

    scpSekizouCheckPoint();

    scpPlayMot(boyGObj, 251);
    scpPlayWaitMotEnd(boyGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    gflagOn(326);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(176, 172), 151, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActStop(sekizo_13a);

    while (stage_CheckAnimationFinish(PAL_JPNKOR(176, 172)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(PAL_JPNKOR(54, 51));
    scpBoyControlReadDisable = 0;
}

void actSt13aElev(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    ScpCallCameraSetTarget(4729.0f, 2715.0f, -2504.0f);

    if (gflagChk(325) == 0) {
        stage_SetAnimation(PAL_JPNKOR(173, 169), 0, 0);

        elev_mes[0].func = actSt13aElevMain;
        act->mail = elev_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt13aSekizo(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(326) == 0) {
        stage_SetAnimation(PAL_JPNKOR(176, 172), 0, 0);

        sekizo_mes[0].func = actSt13aSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(176, 172), 0, -1);
    }
}

void actSt13aCheck(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(327) == 0) {
        check_mes[0].func = actSt13aCheckChk;
        act->mail = check_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt13aChain(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(328) == 0) {
        chainNg_mes[0].func = actSt13aChainNG;
        act->mail = chainNg_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        chainOk_mes[0].func = actSt13aChainOK;
        act->mail = chainOk_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt13aElevMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = elevMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt13aElevSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    lt_switch_layout(PAL_JPNKOR(55, 52));
    scpBoyControlReadDisable = 1;

    elevSwitch_mes[0].func = actSt13aElevUp;
    sub->mail = elevSwitch_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt13aCheckChk(GObj *volatile self)
{
    _ACTWait(1);

    CheckPoint();
    gflagOn(327);
}

void actSt13aChainOK(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerBall(self, scpSearchGobj(2072), 200.0f) != 0) {
        _ACTWait(1);
    }

    EnableChainHang(scpSearchGobj(2071));
    gflagOff(328);

    chainOK_mes[0].func = actSt13aChainNG;
    sub->mail = chainOK_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt13aChainNG(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerBall(self, scpSearchGobj(2072), 200.0f) == 0) {
        _ACTWait(1);
    }

    UnableChainHang(scpSearchGobj(2071));
    gflagOn(328);

    chainNG_mes[0].func = actSt13aChainOK;
    sub->mail = chainNG_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}
