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

/* The functions stand in the order of the stage's source (aug6's TRFILE
   numbers them so). The ones defined inline are output at the end of the
   file, in the order of the declarations below, which gives this build's
   .text order; the others are output where they stand. */
inline void actSt13aElev(GObj *volatile self);
inline void actSt13aElevDown(GObj *volatile self);
inline void actSt13aSekizo(GObj *volatile self);
inline void actSt13aExit(GObj *volatile self);
inline void actSt13aCheck(GObj *volatile self);
inline void actSt13aChain(GObj *volatile self);
inline void actSt13aElevMain(GObj *volatile self);
inline void actSt13aElevSwitch(GObj *volatile self);
inline void actSt13aExitChk(GObj *volatile self);
inline void actSt13aCheckChk(GObj *volatile self);
inline void actSt13aChainOK(GObj *volatile self);
inline void actSt13aChainNG(GObj *volatile self);

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

static ActMail elevMain_mes_pal[2] = {{406, actSt13aElevSwitch}, {429}}; /* derived name */

static ActMail elevMain_mes_ntsc[2] = {{376, actSt13aElevSwitch}, {396}}; /* derived name */

static ActMail elevMain_mes_aug6jun26[2] = {{372, actSt13aElevSwitch}, {392}}; /* derived name */

#define elevMain_mes PAL_NTSC_AUG6JUN26(elevMain_mes_pal, elevMain_mes_ntsc, elevMain_mes_aug6jun26)

static ActMail elev_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail elev_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail elev_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define elev_mes PAL_NTSC_AUG6JUN26(elev_mes_pal, elev_mes_ntsc, elev_mes_aug6jun26)

static ActMail elevSwitch_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail elevSwitch_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail elevSwitch_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define elevSwitch_mes                                                                             \
    PAL_NTSC_AUG6JUN26(elevSwitch_mes_pal, elevSwitch_mes_ntsc, elevSwitch_mes_aug6jun26)

static ActMail elevDown_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail elevDown_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail elevDown_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define elevDown_mes PAL_NTSC_AUG6JUN26(elevDown_mes_pal, elevDown_mes_ntsc, elevDown_mes_aug6jun26)

static ActMail sekizo_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail sekizo_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail sekizo_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define sekizo_mes PAL_NTSC_AUG6JUN26(sekizo_mes_pal, sekizo_mes_ntsc, sekizo_mes_aug6jun26)

static ActMail check_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail check_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail check_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define check_mes PAL_NTSC_AUG6JUN26(check_mes_pal, check_mes_ntsc, check_mes_aug6jun26)

static ActMail chainNg_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail chainNg_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail chainNg_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define chainNg_mes PAL_NTSC_AUG6JUN26(chainNg_mes_pal, chainNg_mes_ntsc, chainNg_mes_aug6jun26)

static ActMail chainOk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail chainOk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail chainOk_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define chainOk_mes PAL_NTSC_AUG6JUN26(chainOk_mes_pal, chainOk_mes_ntsc, chainOk_mes_aug6jun26)

static ActMail chainOK_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail chainOK_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail chainOK_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define chainOK_mes PAL_NTSC_AUG6JUN26(chainOK_mes_pal, chainOK_mes_ntsc, chainOK_mes_aug6jun26)

static ActMail chainNG_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail chainNG_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail chainNG_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define chainNG_mes PAL_NTSC_AUG6JUN26(chainNG_mes_pal, chainNG_mes_ntsc, chainNG_mes_aug6jun26)

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

    stage_SetAnimation(PAL_JPNKORCN(173, 169), 1, 0);
    stage_SetAnimation(PAL_JPNKORCN(174, 170), 1, 0);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(173, 169), 86, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 17);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(173, 169), 140, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    st13a_yure = iosPadActRequest(boyPad, 9);
    st13a_yure_vol = 128;
    iosPadActVolumeSet(st13a_yure, 128);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(173, 169), 200, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    st13a_yure_vol = 64;

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(173, 169), 380, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

static void actSt13aElevUp_ntsc(GObj *volatile self)
{
    scpAdpcmPlayRequestFunc(75, &st13a_up, 0, 1, 0);

    preload(15);

    _ACTWait(60);
    while (st13a_up == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(st13a_up->stream);

    stage_SetAnimation(152, 1, 0);
    stage_SetAnimation(153, 1, 0);

    while (stage_CheckAnimationFrame(152, 86, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 17);

    while (stage_CheckAnimationFrame(152, 140, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    st13a_yure = iosPadActRequest(boyPad, 9);
    st13a_yure_vol = 128;
    iosPadActVolumeSet(st13a_yure, 128);

    while (stage_CheckAnimationFrame(152, 200, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    st13a_yure_vol = 64;

    while (stage_CheckAnimationFrame(152, 380, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(st13a_yure);
    gflagOn(296);
    RequestStageChange(15, boyGObj, 0, 0.025f, 8.0f);
}

static void actSt13aElevUp_aug6jun26(GObj *volatile self)
{
    _ACTWait(60);

    scpAdpcmPlayRequestFunc(75, &st13a_up, 0, 1, 1);
    while (st13a_up == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(151, 1, 0);
    stage_SetAnimation(152, 1, 0);

    while (stage_CheckAnimationFrame(151, 86, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 17);

    while (stage_CheckAnimationFrame(151, 140, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    st13a_yure = iosPadActRequest(boyPad, 9);
    st13a_yure_vol = 128;
    iosPadActVolumeSet(st13a_yure, 128);

    while (stage_CheckAnimationFrame(151, 200, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    st13a_yure_vol = 64;

    while (stage_CheckAnimationFrame(151, 380, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(st13a_yure);
    gflagOn(295);
    RequestStageChange(15, boyGObj, 0, 16.0f, 16.0f);
}

void actSt13aElevUp(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13aElevUp_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13aElevUp_aug6jun26(self);
        return;
    }
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

static void actSt13aElevDown_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(15) != 0) {
        scpFadeOut(255.0f, 0, 0, 0);
        stage_SetAnimation(152, 0, 0);
        lt_switch_layout(51);
        scpBoyControlReadDisable = 1;
        scpPlayStart(boyGObj);
        scpAdpcmPlayRequestFunc(78, &st13a_down, 1, 1, 0);
        _ACTWait(10);
        stage_SetAnimation(152, 0, 451);

        elevDown_mes[0].func = actSt13aElevDownChk;
        act->mail = elevDown_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt13aElevDown_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(15) != 0) {
        scpFadeOut(255.0f, 0, 0, 0);
        stage_SetAnimation(151, 0, 0);
        lt_switch_layout(51);
        scpBoyControlReadDisable = 1;
        scpPlayStart(boyGObj);
        _ACTWait(10);
        stage_SetAnimation(151, 0, 451);

        elevDown_mes[0].func = actSt13aElevDownChk;
        act->mail = elevDown_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt13aElevDown(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13aElevDown_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13aElevDown_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(15) != 0) {
        scpFadeOut(255.0f, 0, 0, 0);
        stage_SetAnimation(PAL_JPNKORCN(173, 169), 0, 0);
        lt_switch_layout(PAL_JPNKORCN(55, 52));
        scpBoyControlReadDisable = 1;
        scpPlayStart(boyGObj);
        scpAdpcmPlayRequestFunc(80, &st13a_down, 1, 1, 0);
        _ACTWait(10);
        stage_SetAnimation(PAL_JPNKORCN(173, 169), 0, 451);

        elevDown_mes[0].func = actSt13aElevDownChk;
        act->mail = elevDown_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static void actSt13aElevDownSub(GObj *volatile self)
{
    stage_SetAnimation(PAL_JPNKORCN(173, 169), 1, 451);
    stage_SetAnimation(PAL_JPNKORCN(175, 171), 1, 0);

    scpPlayPosSet(boyGObj, -4871.0f, -2800.0f, 2699.0f);

    st13a_yure = iosPadActRequest(boyPad, 9);
    st13a_yure_vol = 128;
    iosPadActVolumeSet(st13a_yure, 128);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(173, 169), 850, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 17);
    iosPadActStop(st13a_yure);
    st13a_yure = -1;

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(173, 169)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

static void actSt13aElevDownChk_ntsc(GObj *volatile self)
{
    while (st13a_down == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(st13a_down->stream);
    scpFadeIn(6.0f);

    stage_SetAnimation(152, 1, 451);
    stage_SetAnimation(154, 1, 0);

    scpPlayPosSet(boyGObj, -4871.0f, -2800.0f, 2699.0f);

    st13a_yure = iosPadActRequest(boyPad, 9);
    st13a_yure_vol = 128;
    iosPadActVolumeSet(st13a_yure, 128);

    while (stage_CheckAnimationFrame(152, 850, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 17);
    iosPadActStop(st13a_yure);

    while (stage_CheckAnimationFinish(152) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
    gflagOff(15);
}

static void actSt13aElevDownChk_aug6jun26(GObj *volatile self)
{
    scpAdpcmPlayRequestFunc(78, &st13a_down, 1, 1, 1);
    while (st13a_down == 0) {
        _ACTWait(1);
    }

    scpFadeIn(6.0f);

    stage_SetAnimation(151, 1, 451);
    stage_SetAnimation(153, 1, 0);

    scpPlayPosSet(boyGObj, -4871.0f, -2800.0f, 2699.0f);

    st13a_yure = iosPadActRequest(boyPad, 9);
    st13a_yure_vol = 128;
    iosPadActVolumeSet(st13a_yure, 128);

    while (stage_CheckAnimationFrame(151, 850, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 17);
    iosPadActStop(st13a_yure);

    while (stage_CheckAnimationFinish(151) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
    gflagOff(15);
}

void actSt13aElevDownChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13aElevDownChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13aElevDownChk_aug6jun26(self);
        return;
    }
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
        stage_SetAnimation(PAL_JPNKORCN(175, 171), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(173, 169), 0, -1);
        _ACTWait(2);
        scpPlayPosSet(boyGObj, -4871.0f, 3527.0f, 2699.0f);
        iosPadActStop(st13a_yure);
        scpFadeIn(3.0f);
    }

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    lt_switch_layout(PAL_JPNKORCN(54, 51));
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

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
    scpBoyControlReadDisable = 1;

    scpAdpcmPlayRequestFunc(18, &sekizo13a, 1, 1, 1);
    while (sekizo13a == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(176, 172, 155, 154), 1, 0);

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

    scpPlayMot(boyGObj, PAL_NTSCAUG6JUN26(251, 246));
    scpPlayWaitMotEnd(boyGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    gflagOn(PAL_NTSC_AUG6JUN26(326, 297, 296));

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6JUN26(176, 172, 155, 154), 151, 0) ==
           0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActStop(sekizo_13a);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(176, 172, 155, 154)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));
    scpBoyControlReadDisable = 0;
}

inline void actSt13aExit(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    static ActMail exit_mes[2] = {{393}, {392}}; /* derived name */

    _ACTWait(1);

    exit_mes[0].func = actSt13aExitChk;
    act->mail = exit_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

inline void actSt13aExitChk(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 400.0f) == 0 || gflagChk(15) != 0) {
        _ACTWait(1);
    }

    gflagOn(295);
    RequestStageChange(15, boyGObj, 0, 16.0f, 16.0f);
}

static void actSt13aElev_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    ScpCallCameraSetTarget(4729.0f, 2715.0f, -2504.0f);

    if (gflagChk(296) == 0) {
        stage_SetAnimation(152, 0, 0);

        elev_mes[0].func = actSt13aElevMain;
        act->mail = elev_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt13aElev_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    ScpCallCameraSetTarget(4729.0f, 2715.0f, -2504.0f);

    if (gflagChk(295) == 0) {
        stage_SetAnimation(151, 0, 0);

        elev_mes[0].func = actSt13aElevMain;
        act->mail = elev_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt13aElev(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13aElev_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13aElev_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    ScpCallCameraSetTarget(4729.0f, 2715.0f, -2504.0f);

    if (gflagChk(325) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(173, 169), 0, 0);

        elev_mes[0].func = actSt13aElevMain;
        act->mail = elev_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static void actSt13aSekizo_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(297) == 0) {
        stage_SetAnimation(155, 0, 0);

        sekizo_mes[0].func = actSt13aSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        stage_SetAnimation(155, 0, -1);
    }
}

static inline void actSt13aSekizo_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(296) == 0) {
        stage_SetAnimation(154, 0, 0);

        sekizo_mes[0].func = actSt13aSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    } else {
        stage_SetAnimation(154, 0, -1);
    }
}

void actSt13aSekizo(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13aSekizo_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13aSekizo_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(326) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(176, 172), 0, 0);

        sekizo_mes[0].func = actSt13aSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(176, 172), 0, -1);
    }
}

static void actSt13aCheck_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(298) == 0) {
        check_mes[0].func = actSt13aCheckChk;
        act->mail = check_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt13aCheck_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(297) == 0) {
        check_mes[0].func = actSt13aCheckChk;
        act->mail = check_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt13aCheck(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13aCheck_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13aCheck_aug6jun26(self);
        return;
    }
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

static void actSt13aChain_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(299) == 0) {
        chainNg_mes[0].func = actSt13aChainNG;
        act->mail = chainNg_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        chainOk_mes[0].func = actSt13aChainOK;
        act->mail = chainOk_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt13aChain_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(298) == 0) {
        chainNg_mes[0].func = actSt13aChainNG;
        act->mail = chainNg_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    } else {
        chainOk_mes[0].func = actSt13aChainOK;
        act->mail = chainOk_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt13aChain(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13aChain_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13aChain_aug6jun26(self);
        return;
    }
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

static void actSt13aElevSwitch_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    elevSwitch_mes[0].func = actSt13aElevUp;
    sub->mail = elevSwitch_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt13aElevSwitch_aug6jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    elevSwitch_mes[0].func = actSt13aElevUp;
    sub->mail = elevSwitch_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt13aElevSwitch(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13aElevSwitch_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13aElevSwitch_aug6jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;

    elevSwitch_mes[0].func = actSt13aElevUp;
    sub->mail = elevSwitch_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt13aCheckChk_ntsc(GObj *volatile self)
{
    _ACTWait(1);

    CheckPoint();
    gflagOn(298);
}

static inline void actSt13aCheckChk_aug6jun26(GObj *volatile self)
{
    _ACTWait(1);

    CheckPoint();
    gflagOn(297);
}

void actSt13aCheckChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13aCheckChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13aCheckChk_aug6jun26(self);
        return;
    }
    _ACTWait(1);

    CheckPoint();
    gflagOn(327);
}

static void actSt13aChainOK_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerBall(self, scpSearchGobj(1900), 200.0f) != 0) {
        _ACTWait(1);
    }

    EnableChainHang(scpSearchGobj(1899));
    gflagOff(299);

    chainOK_mes[0].func = actSt13aChainNG;
    sub->mail = chainOK_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt13aChainOK_aug6jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerBall(self, scpSearchGobj(1876), 200.0f) != 0) {
        _ACTWait(1);
    }

    EnableChainHang(scpSearchGobj(1875));
    gflagOff(298);

    chainOK_mes[0].func = actSt13aChainNG;
    sub->mail = chainOK_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt13aChainOK(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13aChainOK_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13aChainOK_aug6jun26(self);
        return;
    }
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

static void actSt13aChainNG_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerBall(self, scpSearchGobj(1900), 200.0f) == 0) {
        _ACTWait(1);
    }

    UnableChainHang(scpSearchGobj(1899));
    gflagOn(299);

    chainNG_mes[0].func = actSt13aChainOK;
    sub->mail = chainNG_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt13aChainNG_aug6jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerBall(self, scpSearchGobj(1876), 200.0f) == 0) {
        _ACTWait(1);
    }

    UnableChainHang(scpSearchGobj(1875));
    gflagOn(298);

    chainNG_mes[0].func = actSt13aChainOK;
    sub->mail = chainNG_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt13aChainNG(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13aChainNG_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13aChainNG_aug6jun26(self);
        return;
    }
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
