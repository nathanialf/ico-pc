#include "st04l.h"
#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
#include "obj_manager.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "act-game.h"
#include "act.h"
#include "gobj_process.h"
#include "boyact.h"
#include "commonact.h"
#include "girl_act.h"
#include "way_llf.h"
#include "camera-root.h"
#include "fightSound.h"
#include "generator.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "st04r.h"
#include "StageAnimation.h"
#include "attackCheckBoundary.h"
#include "motionManager2.h"
#include "weapon.h"
#include <libvu0.h>
#include "typedef.h"
#include "st04r.h"
#include "script.h"
#include "main.h"

static void actSt04eSolarBeamChkSub(GObj *volatile self);

/* .sbss: the four arguments turnBall hands to the ball-turn actor through file
   scope, the demo's own end flag, the crest-2 animation the room selects, and
   the flag the stair subthread raises once it is past its setup. */
static int turnFlag; /* derived name */

static int turnAnim; /* derived name */

static int turnGobj1; /* derived name */

static int turnGobj2; /* derived name */

static int demoEnd;

static int crest2Anim; /* derived name */

static int subStarted; /* derived name */

/* A 16-byte constant vector template: the float view carries the values,
   the long long view is the one the copy reads. */

static const ConstVec stairSubPos = {{0.0f, 0.0f, -5000.0f, 1.0f}}; /* derived name */

/* .sdata: the room's stream handles and shakes (solar4l unused). */
SqEntry *ball1_4l = 0;

SqEntry *ball2_4l = 0;

SqEntry *ball3_4l = 0;

SqEntry *crest1 = 0;

SqEntry *crest2 = 0;

SqEntry *crest3 = 0;

int solar4l = 0;

SqEntry *stair4d = 0;

SqEntry *st04d_hasi = 0;

SqEntry *sekizo4c = 0;

unsigned int oriup4c = 0;

unsigned char oridown4c = 0;

unsigned int st04l_yure = 0;

unsigned char st04l_yure_vol = 0;

void actSt04cInit(void)
{
    if (gflagChk(212) == 0) {
        stage_SetAnimation(234, 0, 0);
    } else {
        stage_SetAnimation(234, 0, -1);
    }

    if (gflagChk(213) == 0) {
        stage_SetAnimation(235, 0, 0);
    } else {
        stage_SetAnimation(235, 0, -1);
    }

    if (gflagChk(214) == 0) {
        stage_SetAnimation(236, 0, 0);
    } else {
        stage_SetAnimation(236, 0, -1);
    }

    if (gflagChk(215) == 0) {
        stage_SetAnimation(237, 0, 0);
    } else {
        stage_SetAnimation(237, 0, -1);
    }

    if (gflagChk(216) == 0) {
        stage_SetAnimation(238, 0, 0);
    } else {
        stage_SetAnimation(238, 0, -1);
    }

    if (gflagChk(217) == 0) {
        stage_SetAnimation(239, 0, 0);
    } else {
        stage_SetAnimation(239, 0, -1);
    }

    if (gflagChk(218) == 0) {
        stage_SetAnimation(240, 0, 0);
    } else {
        stage_SetAnimation(240, 0, -1);
    }

    if (gflagChk(188) == 0) {
        SetWayGroupActive(1, 0);
    } else {
        SetWayGroupActive(1, 1);
    }

    if (gflagChk(182) == 0) {
        stage_SetAnimation(224, 0, 0);
    } else {
        stage_SetAnimation(224, 0, -1);
    }

    if (gflagChk(183) == 0) {
        stage_SetAnimation(225, 0, 0);
    } else {
        stage_SetAnimation(225, 0, -1);
    }
}

void actSt04dInit(void)
{
    if (gflagChk(180) == 0) {
        stage_SetAnimation(227, 0, 0);
        stage_SetAnimation(258, 0, 0);
    } else {
        stage_SetAnimation(227, 0, -1);
        stage_SetAnimation(258, 0, -1);
        FinishHint(17);
    }

    if (gflagChk(182) == 0) {
        stage_SetAnimation(224, 0, 0);
        SetWayGroupActive(3, 0);
    } else {
        stage_SetAnimation(224, 0, -1);
        SetWayGroupActive(3, 1);
    }

    if (gflagChk(183) == 0) {
        stage_SetAnimation(225, 0, 0);
        SetWayGroupActive(4, 0);
    } else {
        stage_SetAnimation(225, 0, -1);
        SetWayGroupActive(4, 1);
    }

    if (gflagChk(212) == 0) {
        stage_SetAnimation(234, 0, 0);
    } else {
        stage_SetAnimation(234, 0, -1);
    }

    if (gflagChk(213) == 0) {
        stage_SetAnimation(235, 0, 0);
    } else {
        stage_SetAnimation(235, 0, -1);
    }

    if (gflagChk(214) == 0) {
        stage_SetAnimation(236, 0, 0);
    } else {
        stage_SetAnimation(236, 0, -1);
    }

    if (gflagChk(215) == 0) {
        stage_SetAnimation(237, 0, 0);
    } else {
        stage_SetAnimation(237, 0, -1);
    }

    if (gflagChk(216) == 0) {
        stage_SetAnimation(238, 0, 0);
    } else {
        stage_SetAnimation(238, 0, -1);
    }

    if (gflagChk(217) == 0) {
        stage_SetAnimation(239, 0, 0);
    } else {
        stage_SetAnimation(239, 0, -1);
    }

    if (gflagChk(218) == 0) {
        stage_SetAnimation(240, 0, 0);
    } else {
        stage_SetAnimation(240, 0, -1);
    }

    if (gflagChk(162) != 0 && gflagChk(174) == 0) {
        stage_SetAnimation(253, 0, -1);
    }

    if (gflagChk(162) == 0 || gflagChk(174) != 0) {
        stage_SetAnimation(253, 0, 0);
    }

    if (gflagChk(190) != 0) {
        stage_SetAnimation(228, 0, 200);
    } else {
        stage_SetAnimation(228, 0, 0);
    }
}

void actSt04eInit(void)
{
    if (gflagChk(230) == 0) {
        SetWayGroupActive(5, 0);
    } else {
        SetWayGroupActive(5, 1);
    }

    if (gflagChk(182) == 0) {
        stage_SetAnimation(224, 0, 0);
    } else {
        stage_SetAnimation(224, 0, -1);
    }

    if (gflagChk(183) == 0) {
        stage_SetAnimation(225, 0, 0);
    } else {
        stage_SetAnimation(225, 0, -1);
    }
}

static void actSt04lBallTurnCommonSub(GObj *volatile self)
{
    _ACTWait(60);

    scpAdpcmPlayRequestFunc(82, &ball1_4l, 1, 1, 1);

    while (ball1_4l == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(turnAnim, 1, 0);

    while (stage_CheckAnimationFinish(turnAnim) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 6);

    demoEnd = 1;
    _ACTWait(0);
}

static void actSt04lBallTurnCommon(GObj *volatile self)
{
    GProc *h;

    lt_switch_layout(55);
    gflagOn(turnFlag);
    scpSleepEnemyAll();

    h = actCreateSubThread(actSt04lBallTurnCommonSub, 21);

    demoEnd = 0;
    ball1_4l = (SqEntry *)ICO_INVALID_PTR;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&h->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        while (ball1_4l == 0) {
            _ACTWait(1);
        }

        if (ball1_4l != (SqEntry *)ICO_INVALID_PTR) {
            scpAdpcmFadeCloseFunc(&ball1_4l, 512);
        }

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(turnAnim, 0, -1);
        scpFadeIn(5.0f);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    }

    scpSearchGobj(turnGobj1)->active = 1;
    scpSearchGobj(turnGobj2)->active = 1;
    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(54);
}

static ActMail c1BallMain_mes[2] = {{406, actSt04lC1BallSwitch}, {429}}; /* derived name */

static ActMail c1Ball_mes[2] = {{430}, {429}}; /* derived name */

static ActMail c1BallSwitch_mes[2] = {{430}, {429}}; /* derived name */

static ActMail c1BallTurn_mes[2] = {{430}, {429}}; /* derived name */

static ActMail turnBall_mes[2] = {{430}, {429}}; /* derived name */

static ActMail c2BallMain_mes[2] = {{406, actSt04lC2BallSwitch}, {429}}; /* derived name */

static ActMail c2Ball_mes[2] = {{430}, {429}}; /* derived name */

static ActMail c2BallSwitch_mes[2] = {{430}, {429}}; /* derived name */

static ActMail c2BallTurn_mes[2] = {{430}, {429}}; /* derived name */

static ActMail c3BallMain_mes[2] = {{407, actSt04lC3BallSwitch}, {429}}; /* derived name */

static ActMail c3Ball_mes[2] = {{430}, {429}}; /* derived name */

static ActMail c3BallSwitch_mes[2] = {{430}, {429}}; /* derived name */

static ActMail c3BallTurn_mes[2] = {{430}, {429}}; /* derived name */

static ActMail crest01_mes[2] = {{430}, {429}}; /* derived name */

static ActMail crest02_mes[2] = {{430}, {429}}; /* derived name */

static ActMail crest03_mes[2] = {{430}, {429}}; /* derived name */

static ActMail st04eSolarBeam_mes[2] = {{430}, {429}}; /* derived name */

static ActMail stair_mes[2] = {{430}, {429}}; /* derived name */

static ActMail rope1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail rope2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail brg1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail brg1Chk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail rope3_mes[2] = {{430}, {429}}; /* derived name */

static ActMail rope4_mes[2] = {{430}, {429}}; /* derived name */

static ActMail brg2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail brg2Chk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail brg1Way_mes[2] = {{430}, {429}}; /* derived name */

static ActMail brg2Way_mes[2] = {{430}, {429}}; /* derived name */

static ActMail sekizo_mes[2] = {{430}, {429}}; /* derived name */

static ActMail turi_mes[2] = {{430}, {429}}; /* derived name */

static ActMail gondola_mes[2] = {{430}, {429}}; /* derived name */

static ActMail gondola2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail gondolaChk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail gondola_chara[2] = {{430}, {429}}; /* derived name */

static ActMail monyou01_mes[2] = {{430}, {429}}; /* derived name */

static ActMail monyou02_mes[2] = {{430}, {429}}; /* derived name */

static ActMail monyou03_mes[2] = {{430}, {429}}; /* derived name */

static ActMail monyou04_mes[2] = {{430}, {429}}; /* derived name */

static ActMail monyou05_mes[2] = {{430}, {429}}; /* derived name */

static ActMail monyou06_mes[2] = {{430}, {429}}; /* derived name */

static ActMail monyou07_mes[2] = {{430}, {429}}; /* derived name */

static ActMail ori_mes[2] = {{430}, {429}}; /* derived name */

static ActMail oriRopeCutR_mes[2] = {{430}, {429}}; /* derived name */

static ActMail oriRopeCutL_mes[2] = {{430}, {429}}; /* derived name */

static ActMail ori2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail sword_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch1_1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch1_2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch2_1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch2_2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch3_1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch3_2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch2_1XL_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch2_2XL_mes[2] = {{430}, {429}}; /* derived name */

void turnBall(GObj *self, int flag, int anim, int gobj1, int gobj2)
{
    Act *sub = GOBJ_ACT(self);

    turnFlag = flag;
    turnAnim = anim;
    turnGobj1 = gobj1;
    turnGobj2 = gobj2;

    turnBall_mes[0].func = actSt04lBallTurnCommon;
    sub->mail = turnBall_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04lCrest02(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(175) == 0) {
        if (current_stage_no == 19) {
            stage_SetAnimation(201, 0, 0);
        } else {
            stage_SetAnimation(202, 0, 0);
        }

        crest02_mes[0].func = actSt04lCrest2Main;
        act->mail = crest02_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        if (current_stage_no == 19) {
            stage_SetAnimation(201, 0, -1);
        } else {
            stage_SetAnimation(202, 0, -1);
        }
    }
}

static void actSt04lCrestSub(GObj *volatile self)
{
    stage_SetAnimation(252, 1, 0);
    stage_SetAnimation(199, 1, 0);

    while (stage_CheckAnimationFrame(199, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    oriup4c = iosPadActRequest(boyPad, 10);
    oridown4c = 128;
    iosPadActVolumeSet(oriup4c, 128);

    while (stage_CheckAnimationFrame(199, 190, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActStop(oriup4c);

    while (stage_CheckAnimationFinish(199) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

void actSt04lCrestMain(GObj *volatile self)
{
    GProc *h;

    while (scpIsTorchLightOn(1114) == 0 || scpIsTorchLightOn(1115) == 0 || gflagChk(177) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    fightSoundProcessRequestPause();

    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    scpAdpcmPlayRequestFunc(27, &ball2_4l, 0, 1, 1);

    while (ball2_4l == 0) {
        _ACTWait(1);
    }

    preload(3);
    gflagOn(174);
    gflagOn(224);

    h = actCreateSubThread(actSt04lCrestSub, 21);

    demoEnd = 0;
    oriup4c = 0xFFFFFFFF;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&h->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        scpAdpcmFadeCloseFunc(&ball2_4l, 256);
        iosPadActStop(oriup4c);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(252, 0, -1);
        stage_SetAnimation(199, 0, -1);
        scpFadeIn(3.0f);
    }

    scpWakeupEnemyAll();
    RequestStageChange(3, boyGObj, 0, 2.0f, 8.0f);
}

static void actSt04lCrest2Sub(GObj *volatile self)
{
    stage_SetAnimation(crest2Anim, 1, 0);

    while (stage_CheckAnimationFrame(crest2Anim, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    oriup4c = iosPadActRequest(boyPad, 10);
    oridown4c = 128;
    iosPadActVolumeSet(oriup4c, 128);

    while (stage_CheckAnimationFrame(crest2Anim, 190, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActStop(oriup4c);
    oriup4c = 0xFFFFFFFF;

    while (stage_CheckAnimationFinish(crest2Anim) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    if (current_stage_no == 19) {
        scpSearchGobj(1122)->active = 1;
    }

    demoEnd = 1;
    _ACTWait(0);
}

void actSt04lCrest2Main(GObj *volatile self)
{
    GProc *h;

    if (current_stage_no == 19) {
        crest2Anim = 201;

        while (scpIsTorchLightOn(1118) == 0 || scpIsTorchLightOn(1119) == 0 || gflagChk(178) == 0) {
            _ACTWait(1);
        }
    }

    if (current_stage_no == 20) {
        crest2Anim = 202;

        while (scpIsTorchLightOn(1202) == 0 || scpIsTorchLightOn(1203) == 0 || gflagChk(178) == 0) {
            _ACTWait(1);
        }
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(26, &ball3_4l, 1, 1, 1);

    while (ball3_4l == 0) {
        _ACTWait(1);
    }

    gflagOn(175);

    h = actCreateSubThread(actSt04lCrest2Sub, 21);

    demoEnd = 0;
    oriup4c = 0xFFFFFFFF;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&h->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        scpAdpcmFadeCloseFunc(&ball3_4l, 256);
        iosPadActStop(oriup4c);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(crest2Anim, 0, -1);

        if (current_stage_no == 19) {
            scpSearchGobj(1122)->active = 1;
        }

        SetCameraFlag_LwsCutBack();
        scpFadeIn(3.0f);
    }

    lt_switch_layout(54);
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
}

static void actSt04lCrest3Sub(GObj *volatile self)
{
    while (stage_CheckAnimationFrame(203, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    oriup4c = iosPadActRequest(boyPad, 10);
    oridown4c = 128;
    iosPadActVolumeSet(oriup4c, 128);

    while (stage_CheckAnimationFrame(203, 190, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActStop(oriup4c);

    while (stage_CheckAnimationFinish(203) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

void actSt04lCrest3Main(GObj *volatile self)
{
    GProc *h;

    while (scpIsTorchLightOn(1204) == 0 || scpIsTorchLightOn(1205) == 0 || gflagChk(179) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(24, &crest1, 0, 1, 1);

    while (crest1 == 0) {
        _ACTWait(1);
    }

    gflagOn(176);
    stage_SetAnimation(203, 1, 0);

    h = actCreateSubThread(actSt04lCrest3Sub, 21);

    demoEnd = 0;
    oriup4c = 0xFFFFFFFF;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&h->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        scpAdpcmFadeCloseFunc(&crest1, 256);
        iosPadActStop(oriup4c);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(203, 0, -1);
        scpFadeIn(3.0f);
    }

    lt_switch_layout(54);
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
}

void actSt04eSolarBeamChk(GObj *volatile self)
{
    GProc *h;

    gflagOff(224);
    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;

    scpAdpcmPlayRequestFunc(28, &crest2, 0, 1, 1);

    while (crest2 == 0) {
        _ACTWait(1);
    }

    preload(7);
    scpFadeIn(16.0f);

    h = actCreateSubThread(actSt04eSolarBeamChkSub, 21);

    demoEnd = 0;

    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&h->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        scpAdpcmFadeCloseFunc(&crest2, 256);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(292, 0, -1);
        scpFadeIn(3.0f);
    }

    RequestStageChange(7, boyGObj, 0, 2.0f, 8.0f);
}

static void actSt04lStairSub(GObj *volatile self)
{
    long long ofs[2];
    float dir[4];

    _ACTWait(60);

    while (crest3 == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(crest3->stream);

    stage_SetAnimation(258, 1, 0);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 532);
    scpPlayPosSet(boyGObj, 55.0f, 28.0f, -3881.0f);
    scpPlayPosSet(girlGObj, -58.0f, 28.0f, -3891.0f);

    ofs[0] = stairSubPos.d[0];
    ofs[1] = stairSubPos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    ofs[0] = stairSubPos.d[0];
    ofs[1] = stairSubPos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    subStarted = 1;

    while (stage_CheckAnimationFinish(258) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 17);

    stage_SetAnimation(227, 1, 0);

    while (stage_CheckAnimationFrame(227, 140, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    oriup4c = iosPadActRequest(boyPad, 9);
    oridown4c = 128;
    iosPadActVolumeSet(oriup4c, 128);

    while (stage_CheckAnimationFinish(227) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

void actSt04lStairChk(GObj *volatile self)
{
    GProc *h;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0xB000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0xB000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyOne(3757);
    gflagOn(180);
    FinishHint(17);

    scpSearchGobj(1242)->active = 0;

    stage_SetAnimation(259, -1, -2);

    scpAdpcmPlayRequestFunc(61, &crest3, 1, 1, 0);

    h = actCreateSubThread(actSt04lStairSub, 21);

    demoEnd = 0;
    subStarted = 0;
    oriup4c = 0xFFFFFFFF;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        while (crest3 == 0) {
            _ACTWait(1);
        }

        scpAdpcmFadeCloseFunc(&crest3, 512);

        while (subStarted == 0) {
            _ACTWait(1);
        }

        iosThreadSetPri(&h->thread, 34);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(258, 0, -1);
        stage_SetAnimation(227, 0, -1);

        _ACTWait(1);

        scpPlayMot(boyGObj, 0);
        scpPlayMot(girlGObj, 532);
        scpPlayPosSet(boyGObj, 55.0f, 234.0f, -3881.0f);
        scpPlayPosSet(girlGObj, -58.0f, 234.0f, -3891.0f);

        _ACTWait(1);
        iosOmSendMail(girlGObj, 62, boyGObj);

        scpFadeIn(3.0f);
    } else {
        iosThreadSetPri(&h->thread, 34);
    }

    iosPadActStop(oriup4c);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 532);
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(54);
    gflagOn(220);
    scpWakeupEnemyOne(3757);
}

void actSt04lRope1Chk(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(184) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1183))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(220, 1, 0);

            while (stage_CheckAnimationFinish(220) == 0) {
                _ACTWait(1);
            }

            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1183)->active = 0;
            gflagOn(184);
            stage_SetAnimation(216, 1, 0);

            while (stage_CheckAnimationFinish(216) == 0) {
                _ACTWait(1);
            }

            _ACTWait(1);
            break;
        }
    }
}

void actSt04lRope2Chk(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(185) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1184))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(221, 1, 0);

            while (stage_CheckAnimationFinish(221) == 0) {
                _ACTWait(1);
            }

            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1184)->active = 0;
            gflagOn(185);
            stage_SetAnimation(217, 1, 0);

            while (stage_CheckAnimationFinish(217) == 0) {
                _ACTWait(1);
            }

            _ACTWait(1);
            break;
        }
    }
}

void actSt04lRope3Chk(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(186) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1185))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(222, 1, 0);

            while (stage_CheckAnimationFinish(222) == 0) {
                _ACTWait(1);
            }

            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1185)->active = 0;
            gflagOn(186);
            stage_SetAnimation(218, 1, 0);

            while (stage_CheckAnimationFinish(218) == 0) {
                _ACTWait(1);
            }

            _ACTWait(1);
            break;
        }
    }
}

void actSt04lRope4Chk(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(187) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1186))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(223, 1, 0);

            while (stage_CheckAnimationFinish(223) == 0) {
                _ACTWait(1);
            }

            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1186)->active = 0;
            gflagOn(187);
            stage_SetAnimation(219, 1, 0);

            while (stage_CheckAnimationFinish(219) == 0) {
                _ACTWait(1);
            }

            _ACTWait(1);
            break;
        }
    }
}

void actSt04lSekizoChk(GObj *volatile self)
{
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0 ||
           scpTriggerFloorAttr(boyGObj, 0x8000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x8000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpKillEnemyAll();
    scpMaskGeneratorAll();

    scpAdpcmPlayRequestFunc(18, &stair4d, 1, 1, 1);

    while (stair4d == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(1, 1);

    stage_SetAnimation(226, 1, 0);

    st04l_yure = iosPadActRequest(boyPad, 9);
    st04l_yure_vol = 128;
    iosPadActVolumeSet(st04l_yure, 128);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 532);
    scpPlayPosSet(girlGObj, 0.0f, -1300.0f, -1700.0f);
    scpPlayPosSet(boyGObj, 20.0f, -1300.0f, -1700.0f);

    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT((void *)self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpBoyControlReadDisable = 1;

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpSekizouCheckPoint();

    scpPlayMot(girlGObj, 645);
    scpPlayWaitMotEnd(girlGObj);

    gflagOn(188);

    while (stage_CheckAnimationFrame(226, 151, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(st04l_yure);

    while (stage_CheckAnimationFinish(226) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(girlGObj, 532);
    scpPlayEnd(girlGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    _ACTWait(1);
    iosOmSendMail(girlGObj, 63, boyGObj);

    lt_switch_layout(54);
    scpBoyControlReadDisable = 0;
}

void actSt04lGondolaChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0xA000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();
    _ACTWait(15);

    if (gflagChk(190) != 0) {
        stage_SetAnimation(228, 1, 200);

        while (stage_CheckAnimationFrame(228, 220, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1321, 0, 0, 1);

        while (stage_CheckAnimationFrame(228, 240, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1322, 0, 0, 1);

        while (stage_CheckAnimationFrame(228, 250, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1319, 0, 0, 1);

        while (stage_CheckAnimationFrame(228, 415, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1320, 0, 0, 1);

        while (stage_CheckAnimationFinish(228) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gflagOff(190);
    } else {
        stage_SetAnimation(228, 1, 0);
        SetGirlDangerGObj(boyGObj);
        soundSeDefPlay(1319, 0, 0, 1);

        while (stage_CheckAnimationFrame(228, 145, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1320, 0, 0, 1);
        soundSeDefPlay(1321, 0, 0, 1);

        while (stage_CheckAnimationFrame(228, 170, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1322, 0, 0, 1);

        while (stage_CheckAnimationFrame(228, 200, 1) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        ClearGirlDangerGObj();
        gflagOn(190);
    }

    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0xA000000) == 0) {
        gflagOn(194);
    } else {
        gflagOff(194);
    }

    scpBoyControlReadDisable = 0;
    lt_switch_layout(54);
    scpWakeupEnemyAll();

    gondolaChk_mes[0].func = actSt04lGondolaCharaChk;
    sub->mail = gondolaChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04lMonyou01Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(212);
    FinishHint(16);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(234, 1, 0);
    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(234, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

void actSt04lMonyou02Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x2000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x2000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(213);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(235, 1, 0);
    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(235, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

void actSt04lMonyou03Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x3000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(214);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(236, 1, 0);
    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(236, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

void actSt04lMonyou04Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x4000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x4000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(215);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(237, 1, 0);
    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(237, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

void actSt04lMonyou05Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x5000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x5000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(216);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(238, 1, 0);
    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(238, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

void actSt04lMonyou06Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x6000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x6000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(217);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(239, 1, 0);
    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(239, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

void actSt04lMonyou07Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x7000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x7000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(218);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(240, 1, 0);
    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(240, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04lOriSub(GObj *volatile self)
{
    _ACTWait(60);

    while (st04d_hasi == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(st04d_hasi->stream);

    stage_SetAnimation(243, 1, 0);
    stage_SetAnimation(245, 1, 0);
    stage_SetAnimation(244, 1, 0);

    while (stage_CheckAnimationFrame(243, 90, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActRequest(boyPad, 15);

    if (girlGObj != 0) {
        scpPlayMotReq(girlGObj, 216);
    }

    while (stage_CheckAnimationFinish(243) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

void actSt04lOriChk(GObj *volatile self)
{
    GProc *h;

    while (gflagChk(175) == 0 || scpGameStat_BoyWeaponkind() != 4 ||
           scpTriggerBall(self, boyGObj, 1000.0f) == 0) {
        _ACTWait(1);
    }

    scpSleepEnemyAll();
    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;

    scpPlayMot(boyGObj, 0);

    if (girlGObj != 0) {
        iosOmSendMail(girlGObj, 62, boyGObj);
    }

    gflagOn(200);
    SetWayGroupActive(14, 0);
    SetWayGroupActive(15, 0);

    scpAdpcmPlayRequestFunc(62, &st04d_hasi, 1, 1, 0);

    h = actCreateSubThread(actSt04lOriSub, 21);

    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&h->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        while (st04d_hasi == 0) {
            _ACTWait(1);
        }

        scpAdpcmFadeCloseFunc(&st04d_hasi, 512);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(243, 0, -1);
        stage_SetAnimation(245, 0, -1);
        stage_SetAnimation(244, 0, -1);
        scpFadeIn(3.0f);
    }

    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(54);
}

void actSt04lOriRopeCutRChk(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (1) {
        if (scpGameStat_BoyWeaponkind() == 4 && scpTriggerBall(self, boyGObj, 100.0f) != 0 &&
            (ForMotionViewer_GetCurrentMotion(boyGObj) == 38 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 43 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 45 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 39 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 40 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 60))
            break;
        _ACTWait(1);
    }

    _ACTWait(15);
    gflagOn(201);

    stage_SetAnimation(246, 1, 0);
    soundSeDefPlay(1323, 0, 0, 1);

    while (stage_CheckAnimationFrame(246, 32, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    soundSeDefPlay(1324, 0, 0, 1);

    while (stage_CheckAnimationFinish(246) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
}

void actSt04lOriRopeCutLChk(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (1) {
        if (scpGameStat_BoyWeaponkind() == 4 && scpTriggerBall(self, boyGObj, 100.0f) != 0 &&
            (ForMotionViewer_GetCurrentMotion(boyGObj) == 38 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 43 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 45 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 39 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 40 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 60))
            break;
        _ACTWait(1);
    }

    _ACTWait(15);
    gflagOn(202);

    stage_SetAnimation(247, 1, 0);
    soundSeDefPlay(1323, 0, 0, 1);

    while (stage_CheckAnimationFrame(247, 32, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    soundSeDefPlay(1324, 0, 0, 1);

    while (stage_CheckAnimationFinish(247) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
}

void actSt04lOri2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(203) == 0) {
        SleepHint(15);

        ori2_mes[0].func = actSt04lOri2Chk;
        act->mail = ori2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(243, -1, -2);
        stage_SetAnimation(245, -1, -2);
        stage_SetAnimation(244, -1, -2);
        stage_SetAnimation(248, 0, -1);
        stage_SetAnimation(247, 0, -1);
        stage_SetAnimation(246, 0, -1);
    }
}

static void actSt04lOri2Sub(GObj *volatile self)
{
    iosPadActRequest(boyPad, 17);
    _ACTWait(30);

    while (sekizo4c == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(sekizo4c->stream);

    stage_SetAnimation(248, 1, 0);

    while (stage_CheckAnimationFrame(248, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActRequest(boyPad, 15);

    if (girlGObj != 0) {
        scpPlayMotReq(girlGObj, 216);
    }

    while (stage_CheckAnimationFinish(248) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

void actSt04lOri2Chk(GObj *volatile self)
{
    GProc *h;

    while (gflagChk(201) == 0 || gflagChk(202) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    gflagOn(203);
    WakeupHint(15);
    SetWayGroupActive(14, 1);
    SetWayGroupActive(15, 1);
    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(63, &sekizo4c, 1, 1, 0);

    h = actCreateSubThread(actSt04lOri2Sub, 21);

    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&h->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        while (sekizo4c == 0) {
            _ACTWait(1);
        }

        scpAdpcmFadeCloseFunc(&sekizo4c, 512);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(248, 0, -1);
        scpFadeIn(3.0f);
    }

    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(54);
}

void actSt04lCrest01(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(174) == 0) {
        stage_SetAnimation(199, 0, 0);

        crest01_mes[0].func = actSt04lCrestMain;
        act->mail = crest01_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(199, 0, -1);

        scpBoyControlReadDisable = 0;
    }
}

void actSt04lCrest03(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(176) == 0) {
        stage_SetAnimation(203, 0, 0);

        crest03_mes[0].func = actSt04lCrest3Main;
        act->mail = crest03_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(203, 0, -1);
    }
}

void actSt04lC1Ball(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(177) == 0) {
        scpSearchGobj(1114)->active = 0;
        scpSearchGobj(1115)->active = 0;

        stage_SetAnimation(207, 0, 0);

        c1Ball_mes[0].func = actSt04lC1BallMain;
        act->mail = c1Ball_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(207, 0, -1);
    }
}

void actSt04lC2Ball(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(178) == 0) {
        scpSearchGobj(1202)->active = 0;
        scpSearchGobj(1203)->active = 0;

        stage_SetAnimation(208, 0, 0);

        c2Ball_mes[0].func = actSt04lC2BallMain;
        act->mail = c2Ball_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(208, 0, -1);
    }
}

void actSt04lC3Ball(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(179) == 0) {
        scpSearchGobj(1204)->active = 0;
        scpSearchGobj(1205)->active = 0;

        stage_SetAnimation(209, 0, 0);

        c3Ball_mes[0].func = actSt04lC3BallMain;
        act->mail = c3Ball_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(209, 0, -1);
    }
}

void actSt04lStair(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(180) == 0) {
        if (girlGObj != 0) {
            stair_mes[0].func = actSt04lStairChk;
            act->mail = stair_mes;
            ACTSendMailCorrect(self, 430);
            _ACTWait(0);
        }
    } else {
        scpSearchGobj(1242)->active = 0;

        stage_SetAnimation(259, -1, -2);
    }
}

void actSt04lBrg1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(182) == 0) {
        brg1_mes[0].func = actSt04lBrg1Chk;
        act->mail = brg1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04lBrg2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(183) == 0) {
        SleepHint(16);

        brg2_mes[0].func = actSt04lBrg2Chk;
        act->mail = brg2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04lBrg1Way(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(182) == 0) {
        brg1Way_mes[0].func = actSt04lBrg1WayChk;
        act->mail = brg1Way_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04lBrg2Way(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(183) == 0) {
        brg2Way_mes[0].func = actSt04lBrg2WayChk;
        act->mail = brg2Way_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04lRope1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(184) == 0) {
        stage_SetAnimation(216, 0, 0);

        rope1_mes[0].func = actSt04lRope1Chk;
        act->mail = rope1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(216, 0, -1);

        scpSearchGobj(1183)->active = 0;
    }
}

void actSt04lRope2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(185) == 0) {
        stage_SetAnimation(217, 0, 0);

        rope2_mes[0].func = actSt04lRope2Chk;
        act->mail = rope2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(217, 0, -1);

        scpSearchGobj(1184)->active = 0;
    }
}

void actSt04lRope3(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(186) == 0) {
        stage_SetAnimation(218, 0, 0);

        rope3_mes[0].func = actSt04lRope3Chk;
        act->mail = rope3_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(218, 0, -1);

        scpSearchGobj(1185)->active = 0;
    }
}

void actSt04lRope4(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(187) == 0) {
        stage_SetAnimation(219, 0, 0);

        rope4_mes[0].func = actSt04lRope4Chk;
        act->mail = rope4_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(219, 0, -1);

        scpSearchGobj(1186)->active = 0;
    }
}

void actSt04lSekizo(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(188) == 0) {
        stage_SetAnimation(226, 0, 0);

        sekizo_mes[0].func = actSt04lSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(226, 0, -1);
    }
}

void actSt04lTuri(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(189) == 0) {
        stage_SetAnimation(230, 0, 0);

        if (girlGObj != 0) {
            turi_mes[0].func = actSt04lTuriChk;
            act->mail = turi_mes;
            ACTSendMailCorrect(self, 430);
            _ACTWait(0);
        }
    } else {
        stage_SetAnimation(230, 0, -1);
    }
}

void actSt04lOri(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(200) == 0) {
        stage_SetAnimation(243, 0, 0);
        stage_SetAnimation(245, 0, 0);
        stage_SetAnimation(244, 0, 0);

        ori_mes[0].func = actSt04lOriChk;
        act->mail = ori_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04lOriRopeCutR(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(201) == 0) {
        oriRopeCutR_mes[0].func = actSt04lOriRopeCutRChk;
        act->mail = oriRopeCutR_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04lOriRopeCutL(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(202) == 0) {
        oriRopeCutL_mes[0].func = actSt04lOriRopeCutLChk;
        act->mail = oriRopeCutL_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04lSword(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(175) == 0) {
        sword_mes[0].func = actSt04lSwordChk;
        act->mail = sword_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04lGondola(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(190) != 0) {
        stage_SetAnimation(228, 0, 200);

        gondola_mes[0].func = actSt04lGondolaChk;
        act->mail = gondola_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(228, 0, 0);

        gondola2_mes[0].func = actSt04lGondolaChk;
        act->mail = gondola2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04lMonyou01(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(212) == 0) {
        stage_SetAnimation(234, 0, 0);

        monyou01_mes[0].func = actSt04lMonyou01Chk;
        act->mail = monyou01_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(234, 0, -1);
        FinishHint(16);
    }
}

void actSt04lMonyou02(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(213) == 0) {
        stage_SetAnimation(235, 0, 0);

        monyou02_mes[0].func = actSt04lMonyou02Chk;
        act->mail = monyou02_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(235, 0, -1);
    }
}

void actSt04lMonyou03(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(214) == 0) {
        stage_SetAnimation(236, 0, 0);

        monyou03_mes[0].func = actSt04lMonyou03Chk;
        act->mail = monyou03_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(236, 0, -1);
    }
}

void actSt04lMonyou04(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(215) == 0) {
        stage_SetAnimation(237, 0, 0);

        monyou04_mes[0].func = actSt04lMonyou04Chk;
        act->mail = monyou04_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(237, 0, -1);
    }
}

void actSt04lMonyou05(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(216) == 0) {
        stage_SetAnimation(238, 0, 0);

        monyou05_mes[0].func = actSt04lMonyou05Chk;
        act->mail = monyou05_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(238, 0, -1);
    }
}

void actSt04lMonyou06(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(217) == 0) {
        stage_SetAnimation(239, 0, 0);

        monyou06_mes[0].func = actSt04lMonyou06Chk;
        act->mail = monyou06_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(239, 0, -1);
    }
}

void actSt04lMonyou07(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(218) == 0) {
        stage_SetAnimation(240, 0, 0);

        monyou07_mes[0].func = actSt04lMonyou07Chk;
        act->mail = monyou07_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(240, 0, -1);
    }
}

void actSt04lCrest01XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(174) == 0) {
        stage_SetAnimation(204, 0, 0);
    } else {
        stage_SetAnimation(204, 0, -1);
    }
}

void actSt04lCrest02XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(175) == 0) {
        stage_SetAnimation(205, 0, 0);
    } else {
        stage_SetAnimation(205, 0, -1);
    }
}

void actSt04lCrest03XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(176) == 0) {
        stage_SetAnimation(206, 0, 0);
    } else {
        stage_SetAnimation(206, 0, -1);
    }
}

void actSt04lC2BallXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(178) == 0) {
        scpSearchGobj(1118)->active = 0;
        scpSearchGobj(1119)->active = 0;

        stage_SetAnimation(208, 0, 0);
    } else {
        stage_SetAnimation(208, 0, -1);
    }
}

void actSt04lC3BallXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(179) == 0) {
        stage_SetAnimation(209, 0, 0);

        scpSearchGobj(1283)->active = 0;
        scpSearchGobj(1284)->active = 0;
    } else {
        stage_SetAnimation(209, 0, -1);
    }
}

void actSt04lTorch1_1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(204) == 0) {
        torch1_1_mes[0].func = actSt04lTorch1_1Chk;
        act->mail = torch1_1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        scpTorchLightOn(1114);
        stage_SetAnimation(210, 0, -1);
    }
}

void actSt04lTorch1_2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(205) == 0) {
        torch1_2_mes[0].func = actSt04lTorch1_2Chk;
        act->mail = torch1_2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        scpTorchLightOn(1115);
        stage_SetAnimation(211, 0, -1);
    }
}

void actSt04lTorch2_1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(206) != 0 || gflagChk(208) != 0) {
        scpTorchLightOn(1202);
        stage_SetAnimation(212, 0, -1);
    } else {
        torch2_1_mes[0].func = actSt04lTorch2_1Chk;
        act->mail = torch2_1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04lTorch2_2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(207) != 0 || gflagChk(209) != 0) {
        scpTorchLightOn(1203);
        stage_SetAnimation(213, 0, -1);
    } else {
        torch2_2_mes[0].func = actSt04lTorch2_2Chk;
        act->mail = torch2_2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04lTorch3_1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(210) == 0) {
        torch3_1_mes[0].func = actSt04lTorch3_1Chk;
        act->mail = torch3_1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        scpTorchLightOn(1204);
        stage_SetAnimation(214, 0, -1);
    }
}

void actSt04lTorch3_2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(211) == 0) {
        torch3_2_mes[0].func = actSt04lTorch3_2Chk;
        act->mail = torch3_2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        scpTorchLightOn(1205);
        stage_SetAnimation(215, 0, -1);
    }
}

void actSt04lTorch2_1XL(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(206) != 0 || gflagChk(208) != 0) {
        scpTorchLightOn(1118);
        stage_SetAnimation(212, 0, -1);
    } else {
        torch2_1XL_mes[0].func = actSt04lTorch2_1XLChk;
        act->mail = torch2_1XL_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04lTorch2_2XL(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(207) != 0 || gflagChk(209) != 0) {
        scpTorchLightOn(1119);
        stage_SetAnimation(213, 0, -1);
    } else {
        torch2_2XL_mes[0].func = actSt04lTorch2_2XLChk;
        act->mail = torch2_2XL_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04lTorch3_1XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(210) != 0) {
        scpTorchLightOn(1283);
    }
}

void actSt04lTorch3_2XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(211) != 0) {
        scpTorchLightOn(1284);
    }
}

void actSt04cDoorInit(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(162) != 0 && gflagChk(174) == 0) {
        stage_SetAnimation(253, 0, -1);
    }

    if (gflagChk(162) == 0 || gflagChk(174) != 0) {
        stage_SetAnimation(253, 0, 0);
    }
}

void actSt04dEnemy1(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);

    while (gflagChk(220) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt04dEnemy2(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);

    while (gflagChk(220) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt04dEnemy3(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);

    while (gflagChk(220) == 0) {
        _ACTWait(1);
    }

    Generator_Call(self);
}

void actSt04eSolarBeam(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(224) != 0) {
        if (boyGObj != 0) {
            scpPlayMot(boyGObj, 0);

            if (ACTGame_NoWeapon(boyGObj) == 0) {
                LightTorchOffOfWeapon(GetBoyWeaponGObj());
            }
        }

        gflagOn(389);

        scpSeEnvMasterVolRate = 0.0f;
        scpFadeOut(255.0f, 0, 0, 0);

        st04eSolarBeam_mes[0].func = actSt04eSolarBeamChk;
        act->mail = st04eSolarBeam_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04lC1BallMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = c1BallMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt04lC1BallSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    c1BallSwitch_mes[0].func = actSt04lC1BallTurn;
    sub->mail = c1BallSwitch_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04lC1BallTurn(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    turnFlag = 177;
    turnAnim = 207;
    turnGobj1 = 1114;
    turnGobj2 = 1115;

    c1BallTurn_mes[0].func = actSt04lBallTurnCommon;
    act->mail = c1BallTurn_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04lC2BallMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = c2BallMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt04lC2BallSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    c2BallSwitch_mes[0].func = actSt04lC2BallTurn;
    sub->mail = c2BallSwitch_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04lC2BallTurn(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    turnFlag = 178;
    turnAnim = 208;
    turnGobj1 = 1202;
    turnGobj2 = 1203;

    c2BallTurn_mes[0].func = actSt04lBallTurnCommon;
    act->mail = c2BallTurn_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04lC3BallMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = c3BallMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt04lC3BallSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    c3BallSwitch_mes[0].func = actSt04lC3BallTurn;
    sub->mail = c3BallSwitch_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04lC3BallTurn(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    turnFlag = 179;
    turnAnim = 209;
    turnGobj1 = 1204;
    turnGobj2 = 1205;

    c3BallTurn_mes[0].func = actSt04lBallTurnCommon;
    act->mail = c3BallTurn_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt04eSolarBeamChkSub(GObj *volatile self)
{
    stage_SetAnimation(292, 1, 0);

    while (stage_CheckAnimationFinish(292) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

void actSt04lRope1Event(int x)
{
    volatile int local = x;
}

void actSt04lRope2Event(int x)
{
    volatile int local = x;
}

void actSt04lBrg1Event(int x)
{
    volatile int local = x;
}

void actSt04lBrg1Chk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (gflagChk(184) == 0 || gflagChk(185) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();
    gflagOn(182);

    sekizo_4r = 224;

    brg1Chk_mes[0].func = actSt04rBrgCommon;
    sub->mail = brg1Chk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04lRope3Event(int x)
{
    volatile int local = x;
}

void actSt04lRope4Event(int x)
{
    volatile int local = x;
}

void actSt04lBrg2Event(int x)
{
    volatile int local = x;
}

void actSt04lBrg2Chk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (gflagChk(186) == 0 || gflagChk(187) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();
    gflagOn(183);
    WakeupHint(16);

    sekizo_4r = 225;

    brg2Chk_mes[0].func = actSt04rBrgCommon;
    sub->mail = brg2Chk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04lBrg1WayChk(GObj *volatile self)
{
    while (gflagChk(182) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(3, 1);
}

void actSt04lBrg2WayChk(GObj *volatile self)
{
    while (gflagChk(182) == 0 || gflagChk(183) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(4, 1);
}

void actSt04lTuriEvent(int x)
{
    volatile int local = x;
}

void actSt04lTuriChk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, girlGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);
    gflagOn(189);
    _ACTWait(10);

    stage_SetAnimation(230, 1, 0);

    while (stage_CheckAnimationFinish(230) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    lt_switch_layout(54);
}

void actSt04lGondolaCharaChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0xA000000)) {
        if (gflagChk(194) && scpTriggerFloorAttr(girlGObj, 0xA000000)) {
            break;
        }
        _ACTWait(1);
    }

    gondola_chara[0].func = actSt04lGondolaChk;
    sub->mail = gondola_chara;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04lMonyou01Event(int x)
{
    volatile int local = x;
}

void actSt04lMonyou02Event(int x)
{
    volatile int local = x;
}

void actSt04lMonyou03Event(int x)
{
    volatile int local = x;
}

void actSt04lMonyou04Event(int x)
{
    volatile int local = x;
}

void actSt04lMonyou05Event(int x)
{
    volatile int local = x;
}

void actSt04lMonyou06Event(int x)
{
    volatile int local = x;
}

void actSt04lMonyou07Event(int x)
{
    volatile int local = x;
}

void actSt04lOriEvent(int x)
{
    volatile int local = x;
}

void actSt04lOri2Event(int x)
{
    volatile int local = x;
}

void actSt04lSwordChk(GObj *volatile self)
{
    scpSearchGobj(1122)->active = 0;
}

void actSt04lTorch1_1Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(1114) == 0) {
        _ACTWait(1);
    }

    gflagOn(204);
    stage_SetAnimation(210, 1, 0);
}

void actSt04lTorch1_2Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(1115) == 0) {
        _ACTWait(1);
    }

    gflagOn(205);
    stage_SetAnimation(211, 1, 0);
}

void actSt04lTorch2_1Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(1202) == 0) {
        _ACTWait(1);
    }

    gflagOn(206);
    stage_SetAnimation(212, 1, 0);
}

void actSt04lTorch2_2Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(1203) == 0) {
        _ACTWait(1);
    }

    gflagOn(207);
    stage_SetAnimation(213, 1, 0);
}

void actSt04lTorch3_1Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(1204) == 0) {
        _ACTWait(1);
    }

    gflagOn(210);
    stage_SetAnimation(214, 1, 0);
}

void actSt04lTorch3_2Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(1205) == 0) {
        _ACTWait(1);
    }

    gflagOn(211);
    stage_SetAnimation(215, 1, 0);
}

void actSt04lTorch2_1XLChk(GObj *volatile self)
{
    while (scpIsTorchLightOn(1118) == 0) {
        _ACTWait(1);
    }

    gflagOn(208);
    stage_SetAnimation(212, 1, 0);
}

void actSt04lTorch2_2XLChk(GObj *volatile self)
{
    while (scpIsTorchLightOn(1119) == 0) {
        _ACTWait(1);
    }

    gflagOn(209);
    stage_SetAnimation(213, 1, 0);
}
