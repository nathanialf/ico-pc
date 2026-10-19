#include "st04r.h"
#include "st04l.h"
#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
#include "gobj_process.h"
#include "obj_manager.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "act-game.h"
#include "act.h"
#include "commonact.h"
#include "girl_act.h"
#include "way_llf.h"
#include "camera-root.h"
#include "fightSound.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "attackCheckBoundary.h"
#include "item.h"
#include <libvu0.h>
#include "typedef.h"
#include "script.h"
#include "main.h"

static void actSt04rBrg1Sub(GObj *volatile self);
static void openGate(struct GObj *self);

static ActMail c1_ball_main_mes[2] = {{406, actSt04rC1BallSwitch}, {429}}; /* derived name */

static ActMail c1_ball_mes[2] = {{430}, {429}}; /* derived name */

static ActMail c1_ball_switch_mes[2] = {{430}, {429}}; /* derived name */

static ActMail c2_ball_main_mes[2] = {{406, actSt04rC2BallSwitch}, {429}}; /* derived name */

static ActMail c2_ball_mes[2] = {{430}, {429}}; /* derived name */

static ActMail c2_ball_switch_mes[2] = {{430}, {429}}; /* derived name */

static ActMail c3_ball_main_mes[2] = {{407, actSt04rC3BallSwitch}, {429}}; /* derived name */

static ActMail c3_ball_mes[2] = {{430}, {429}}; /* derived name */

static ActMail c3_ball_switch_mes[2] = {{430}, {429}}; /* derived name */

static ActMail crest01_mes[2] = {{430}, {429}}; /* derived name */

static ActMail crest02_mes[2] = {{430}, {429}}; /* derived name */

static ActMail crest03_mes[2] = {{430}, {429}}; /* derived name */

static ActMail solar_beam_mes[2] = {{430}, {429}}; /* derived name */

static ActMail solar_stage_change_mes[2] = {{430}, {429}}; /* derived name */

static ActMail stair_mes[2] = {{430}, {429}}; /* derived name */

static ActMail sekizo_mes[2] = {{430}, {429}}; /* derived name */

static ActMail rope1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail rope2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail brg1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail brg1_chk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail rope3_mes[2] = {{430}, {429}}; /* derived name */

static ActMail rope4_mes[2] = {{430}, {429}}; /* derived name */

static ActMail brg2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail brg2_chk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail brg1_way_mes[2] = {{430}, {429}}; /* derived name */

static ActMail brg2_way_mes[2] = {{430}, {429}}; /* derived name */

static ActMail monyou01_mes[2] = {{430}, {429}}; /* derived name */

static ActMail monyou02_mes[2] = {{430}, {429}}; /* derived name */

static ActMail monyou03_mes[2] = {{430}, {429}}; /* derived name */

static ActMail monyou04_mes[2] = {{430}, {429}}; /* derived name */

static ActMail monyou05_mes[2] = {{430}, {429}}; /* derived name */

static ActMail monyou06_mes[2] = {{430}, {429}}; /* derived name */

static ActMail monyou07_mes[2] = {{430}, {429}}; /* derived name */

static ActMail barricade_mes[2] = {{430}, {429}}; /* derived name */

static ActMail gondola_mes[2] = {{430}, {429}}; /* derived name */

static ActMail gondola2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail gondola_chk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail gondola_chara_chk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch1_1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch1_2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch2_1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch2_2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch3_1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch3_2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch2_1xl_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch2_2xl_mes[2] = {{430}, {429}}; /* derived name */

/* .sbss: the pad actuator handle openGate holds, the demo's own end flag, the
   gate animation and voice clip openGate plays, and the flag the stair
   subthread raises once it is past its setup. */
static int padAct; /* derived name */

static int demoEnd;

static int gateAnim; /* derived name */

static int gateAdpcm; /* derived name */

static int subStarted; /* derived name */

/* The look-at point both characters face in the crest cutscene. */
static const ConstVec crestFacePos = {{0.0f, 0.0f, -5000.0f, 1.0f}}; /* derived name */

/* .sdata: the room's stream handles and shakes (the three balls and crest2_4r
   unused). */
SqEntry *solar4r = 0;

int ball1_4r = 0;

int ball2_4r = 0;

int ball3_4r = 0;

SqEntry *crest1_4r = 0;

int crest2_4r = 0;

SqEntry *crest3_4r = 0;

SqEntry *stair5d = 0;

SqEntry *sekizo5c = 0;

unsigned char st05d_hasi = 0;

int st04r_yure = 0;

unsigned char st04r_yure_vol = 0;

int sekizo_4r = 0;

void actSt05eEnd(void)
{
    if (solar4r != 0) {
        if (gflagChk(234) == 0) {
            scpAdpcmCloseFunc(&solar4r);
        }
    }
}

void actSt05cInit(void)
{
    if (gflagChk(274) == 0) {
        stage_SetAnimation(PAL_JPNKOR(337, 333), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(337, 333), 0, -1);
    }

    if (gflagChk(275) == 0) {
        stage_SetAnimation(PAL_JPNKOR(338, 334), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(338, 334), 0, -1);
    }

    if (gflagChk(276) == 0) {
        stage_SetAnimation(PAL_JPNKOR(339, 335), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(339, 335), 0, -1);
    }

    if (gflagChk(277) == 0) {
        stage_SetAnimation(PAL_JPNKOR(340, 336), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(340, 336), 0, -1);
    }

    if (gflagChk(278) == 0) {
        stage_SetAnimation(PAL_JPNKOR(341, 337), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(341, 337), 0, -1);
    }

    if (gflagChk(279) == 0) {
        stage_SetAnimation(PAL_JPNKOR(342, 338), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(342, 338), 0, -1);
    }

    if (gflagChk(280) == 0) {
        stage_SetAnimation(PAL_JPNKOR(343, 339), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(343, 339), 0, -1);
    }

    if (gflagChk(254) == 0) {
        SetWayGroupActive(1, 0);
    } else {
        SetWayGroupActive(1, 1);
    }

    if (gflagChk(259) == 0) {
        stage_SetAnimation(PAL_JPNKOR(328, 324), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(328, 324), 0, -1);
    }

    if (gflagChk(260) == 0) {
        stage_SetAnimation(PAL_JPNKOR(329, 325), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(329, 325), 0, -1);
    }

    if (gflagChk(253) == 0) {
        scpSearchGobj(1379)->active = 0;
    }
}

void actSt05dInit(void)
{
    if (gflagChk(259) == 0) {
        stage_SetAnimation(PAL_JPNKOR(328, 324), 0, 0);

        SetWayGroupActive(3, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(328, 324), 0, -1);

        SetWayGroupActive(3, 1);
    }

    if (gflagChk(260) == 0) {
        stage_SetAnimation(PAL_JPNKOR(329, 325), 0, 0);

        SetWayGroupActive(4, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(329, 325), 0, -1);

        SetWayGroupActive(4, 1);
    }

    if (gflagChk(253) == 0) {
        stage_SetAnimation(PAL_JPNKOR(330, 326), 0, 0);
        stage_SetAnimation(PAL_JPNKOR(350, 346), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(330, 326), 0, -1);
        stage_SetAnimation(PAL_JPNKOR(350, 346), 0, -1);

        FinishHint(24);
    }

    if (gflagChk(274) == 0) {
        stage_SetAnimation(PAL_JPNKOR(337, 333), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(337, 333), 0, -1);
    }

    if (gflagChk(275) == 0) {
        stage_SetAnimation(PAL_JPNKOR(338, 334), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(338, 334), 0, -1);
    }

    if (gflagChk(276) == 0) {
        stage_SetAnimation(PAL_JPNKOR(339, 335), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(339, 335), 0, -1);
    }

    if (gflagChk(277) == 0) {
        stage_SetAnimation(PAL_JPNKOR(340, 336), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(340, 336), 0, -1);
    }

    if (gflagChk(278) == 0) {
        stage_SetAnimation(PAL_JPNKOR(341, 337), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(341, 337), 0, -1);
    }

    if (gflagChk(279) == 0) {
        stage_SetAnimation(PAL_JPNKOR(342, 338), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(342, 338), 0, -1);
    }

    if (gflagChk(280) == 0) {
        stage_SetAnimation(PAL_JPNKOR(343, 339), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(343, 339), 0, -1);
    }

    if (gflagChk(165) != 0 && gflagChk(234) == 0) {
        stage_SetAnimation(PAL_JPNKOR(347, 343), 0, -1);
    }

    if (gflagChk(165) == 0 || gflagChk(234) != 0) {
        stage_SetAnimation(PAL_JPNKOR(347, 343), 0, 0);
    }

    if (gflagChk(282) != 0) {
        stage_SetAnimation(PAL_JPNKOR(229, 225), 0, 200);
    } else {
        stage_SetAnimation(PAL_JPNKOR(229, 225), 0, 0);
    }
}

void actSt05eInit(void)
{
    if (gflagChk(231) == 0) {
        SetWayGroupActive(5, 0);
    } else {
        SetWayGroupActive(5, 1);
    }

    if (gflagChk(259) == 0) {
        stage_SetAnimation(PAL_JPNKOR(328, 324), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(328, 324), 0, -1);
    }

    if (gflagChk(260) == 0) {
        stage_SetAnimation(PAL_JPNKOR(329, 325), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(329, 325), 0, -1);
    }
}

void actSt04rCrest02(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(244) == 0) {
        if (current_stage_no == 28) {
            stage_SetAnimation(PAL_JPNKOR(311, 307), 0, 0);
        } else {
            stage_SetAnimation(PAL_JPNKOR(312, 308), 0, 0);
        }

        crest02_mes[0].func = actSt04rCrest2Main;
        act->mail = crest02_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        if (current_stage_no == 28) {
            stage_SetAnimation(PAL_JPNKOR(311, 307), 0, -1);
        } else {
            stage_SetAnimation(PAL_JPNKOR(312, 308), 0, -1);
        }

        scpBoyControlReadDisable = 0;
    }
}

static void openGateSub(GObj *volatile self)
{
    stage_SetAnimation(gateAnim, 1, 0);

    while (stage_CheckAnimationFrame(gateAnim, 30, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    padAct = iosPadActRequest(boyPad, 10);
    st05d_hasi = 128;
    iosPadActVolumeSet(padAct, 128);

    while (stage_CheckAnimationFrame(gateAnim, 190, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(padAct);

    while (stage_CheckAnimationFinish(gateAnim) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

static void openGate(GObj *self)
{
    GProc *th;

    lt_switch_layout(PAL_JPNKOR(55, 52));

    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(gateAdpcm, &crest1_4r, 0, 1, 1);

    while (crest1_4r == 0) {
        _ACTWait(1);
    }

    th = actCreateSubThread(openGateSub, 21);

    demoEnd = 0;
    padAct = -1;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&((GProc *)th)->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        scpAdpcmFadeCloseFunc(&crest1_4r, 256);

        if (padAct >= 0) {
            iosPadActStop(padAct);
        }

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(gateAnim, 0, -1);

        scpFadeIn(3.0f);
    }

    scpWakeupEnemyAll();

    lt_switch_layout(PAL_JPNKOR(54, 51));

    scpBoyControlReadDisable = 0;
}

void actSt04rCrest2Main(GObj *volatile self)
{
    int anim;

    if (current_stage_no == 28) {
        anim = PAL_JPNKOR(311, 307);
        while (scpIsTorchLightOn(1408) == 0 || scpIsTorchLightOn(1409) == 0 || gflagChk(251) == 0) {
            _ACTWait(1);
        }

        gateAnim = anim;
    }

    if (current_stage_no == 29) {
        anim = PAL_JPNKOR(312, 308);

        while (scpIsTorchLightOn(1487) == 0 || scpIsTorchLightOn(1488) == 0 || gflagChk(251) == 0) {
            _ACTWait(1);
        }

        gateAnim = anim;
    }

    gateAdpcm = 25;
    openGate(self);

    if (current_stage_no == 28) {
        gflagOn(248);
    } else {
        gflagOn(247);
    }

    _ACTWait(1);

    gflagOn(244);
    gflagOff(246);
    gflagOff(249);
    gflagOff(233);
}

void actSt04rCrest3Main(GObj *volatile self)
{
    while (scpIsTorchLightOn(1489) == 0 || scpIsTorchLightOn(1490) == 0 || gflagChk(252) == 0) {
        _ACTWait(1);
    }

    scpSleepEnemyOne(PAL_JPNKOR(3757, 3734));

    gflagOn(170);

    fightSoundProcessRequestPause();

    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    gateAnim = PAL_JPNKOR(313, 309);
    gateAdpcm = 24;
    openGate(self);

    scpWakeupEnemyOne(PAL_JPNKOR(3757, 3734));

    fightSoundProcessRequestStart();
    _ACTWait(1);

    gflagOn(245);
    gflagOff(246);
    gflagOff(247);
    gflagOff(248);
    gflagOn(249);
    gflagOff(233);
}

void actSt04rSolarBeam(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    solar4r = 0;

    if (gflagChk(234) == 0) {
        scpAdpcmPlayRequestFunc(28, &solar4r, 0, 1, 0);

        if (gflagChk(243) != 0 && gflagChk(244) != 0 && gflagChk(245) != 0 && gflagChk(232) != 0) {
            if (boyGObj != 0) {
                scpPlayMot(boyGObj, 0);
            }

            lt_switch_layout(PAL_JPNKOR(55, 52));

            scpBoyControlReadDisable = 1;
            gflagOn(389);

            scpFadeOut(255.0f, 0, 0, 0);

            while (solar4r == 0) {
                _ACTWait(1);
            }

            scpFadeIn(6.0f);
        }

        solar_beam_mes[0].func = actSt04rSolarBeamChk;
        act->mail = solar_beam_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        scpBoyControlReadDisable = 0;
    }
}

void actSt04rSolarBeamChk(GObj *volatile self)
{
    while (gflagChk(243) == 0 || gflagChk(244) == 0 || gflagChk(245) == 0 || gflagChk(232) == 0) {
        _ACTWait(1);
    }

    gflagOn(234);

    lt_switch_layout(PAL_JPNKOR(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    while (solar4r == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(solar4r->stream);

    preload(15);

    stage_SetAnimation(PAL_JPNKOR(346, 342), 1, 0);
    stage_SetAnimation(PAL_JPNKOR(294, 290), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKOR(294, 290)) == 0) {
        if ((pad[0].flags & 0x800) != 0 && scpAdpcmPlayRequestNum() == 0) {
            scpFadeOut(16.0f, 0, 0, 0);

            scpAdpcmFadeCloseFunc(&solar4r, 256);

            while (scpFadeChk() != 0) {
                _ACTWait(1);
            }
            while (lt_fade_status() != 2) {
                _ACTWait(1);
            }

            stage_SetAnimation(PAL_JPNKOR(346, 342), 0, -1);
            stage_SetAnimation(PAL_JPNKOR(294, 290), 0, -1);

            scpFadeIn(3.0f);
            break;
        }
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 0;

    scpWakeupEnemyAll();

    RequestStageChange(15, boyGObj, 0, 2.0f, 8.0f);
}

static void actSt04rStairSub(GObj *volatile self)
{
    long long buf[2];
    float dir[4];

    _ACTWait(60);

    while (crest3_4r == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(crest3_4r->stream);

    if (ICO_JPNKOR) {
        stage_SetAnimation(346, 1, 0);
    } else {
        stage_SetAnimation(350, 1, 0);

        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
    }

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 532);

    scpPlayPosSet(boyGObj, -55.0f, 28.0f, -3881.0f);
    scpPlayPosSet(girlGObj, 58.0f, 28.0f, -3891.0f);

    buf[0] = crestFacePos.d[0];
    buf[1] = crestFacePos.d[1];
    sceVu0SubVector(dir, buf, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    buf[0] = crestFacePos.d[0];
    buf[1] = crestFacePos.d[1];
    sceVu0SubVector(dir, buf, test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    subStarted = 1;

    while (stage_CheckAnimationFinish(PAL_JPNKOR(350, 346)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 17);

    stage_SetAnimation(PAL_JPNKOR(330, 326), 1, 0);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(330, 326), 140, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    padAct = iosPadActRequest(boyPad, 9);
    st05d_hasi = 128;
    iosPadActVolumeSet(padAct, 128);

    while (stage_CheckAnimationFinish(PAL_JPNKOR(330, 326)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

void actSt04rStairChk(GObj *volatile self)
{
    GProc *th;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0xB000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0xB000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKOR(55, 52));

    scpBoyControlReadDisable = 1;
    if (ICO_JPNKOR) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
    }
    gflagOn(253);

    FinishHint(24);

    scpSleepEnemyAll();

    scpSearchGobj(1523)->active = 0;

    stage_SetAnimation(PAL_JPNKOR(351, 347), -1, -2);

    scpAdpcmPlayRequestFunc(61, &crest3_4r, 1, 1, 0);

    th = actCreateSubThread(actSt04rStairSub, 21);

    demoEnd = 0;
    padAct = -1;
    subStarted = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        while (crest3_4r == 0) {
            _ACTWait(1);
        }

        scpAdpcmFadeCloseFunc(&crest3_4r, 512);

        while (subStarted == 0) {
            _ACTWait(1);
        }

        iosThreadSetPri(&((GProc *)th)->thread, 34);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(PAL_JPNKOR(350, 346), 0, -1);
        stage_SetAnimation(PAL_JPNKOR(330, 326), 0, -1);
        _ACTWait(1);

        scpPlayMot(boyGObj, 0);
        scpPlayMot(girlGObj, 532);

        scpPlayPosSet(boyGObj, -55.0f, 234.0f, -3881.0f);
        scpPlayPosSet(girlGObj, 58.0f, 234.0f, -3891.0f);

        _ACTWait(1);

        iosOmSendMail(girlGObj, 62, boyGObj);

        scpFadeIn(3.0f);
    } else {
        iosThreadSetPri(&((GProc *)th)->thread, 34);
    }

    if (padAct >= 0) {
        iosPadActStop(padAct);
    }

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 532);

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;

    lt_switch_layout(PAL_JPNKOR(54, 51));
}

void actSt04rSekizoChk(GObj *volatile self)
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

    lt_switch_layout(PAL_JPNKOR(55, 52));

    scpBoyControlReadDisable = 1;
    scpKillEnemyAll();

    scpMaskGeneratorAll();

    scpAdpcmPlayRequestFunc(18, &stair5d, 1, 1, 1);

    while (stair5d == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(1, 1);

    stage_SetAnimation(PAL_JPNKOR(331, 327), 1, 0);

    st04r_yure = iosPadActRequest(boyPad, 9);
    st04r_yure_vol = 128;
    iosPadActVolumeSet(st04r_yure, 128);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 532);

    scpPlayPosSet(girlGObj, 0.0f, -1300.0f, -1700.0f);
    scpPlayPosSet(boyGObj, 20.0f, -1300.0f, -1700.0f);
    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpBoyControlReadDisable = 1;

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpSekizouCheckPoint();

    scpPlayMot(girlGObj, 645);
    scpPlayWaitMotEnd(girlGObj);

    gflagOn(254);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(331, 327), 151, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(st04r_yure);

    while (stage_CheckAnimationFinish(PAL_JPNKOR(331, 327)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(girlGObj, 532);
    scpPlayEnd(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    _ACTWait(1);

    iosOmSendMail(girlGObj, 63, boyGObj);

    lt_switch_layout(PAL_JPNKOR(54, 51));

    scpBoyControlReadDisable = 0;
}

void actSt04rRope1Chk(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(261) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1459))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(PAL_JPNKOR(324, 320), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKOR(324, 320)) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1459)->active = 0;

            gflagOn(261);

            stage_SetAnimation(PAL_JPNKOR(320, 316), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKOR(320, 316)) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        }
    }
}

void actSt04rRope2Chk(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(262) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1460))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(PAL_JPNKOR(325, 321), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKOR(325, 321)) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1460)->active = 0;

            gflagOn(262);

            stage_SetAnimation(PAL_JPNKOR(321, 317), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKOR(321, 317)) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        }
    }
}

void actSt04rBrgCommon(GObj *volatile self)
{
    GProc *th;

    lt_switch_layout(PAL_JPNKOR(55, 52));

    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(86, &sekizo5c, 1, 1, 0);

    th = actCreateSubThread(actSt04rBrg1Sub, 21);

    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&((GProc *)th)->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        scpAdpcmFadeCloseFunc(&sekizo5c, 256);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(sekizo_4r, 0, -1);

        scpFadeIn(3.0f);
    }

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;

    lt_switch_layout(PAL_JPNKOR(54, 51));
}

void actSt04rRope3Chk(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(263) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1461))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(PAL_JPNKOR(326, 322), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKOR(326, 322)) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1461)->active = 0;

            gflagOn(263);

            stage_SetAnimation(PAL_JPNKOR(322, 318), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKOR(322, 318)) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        }
    }
}

void actSt04rRope4Chk(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(264) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(1462))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(PAL_JPNKOR(327, 323), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKOR(327, 323)) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(1462)->active = 0;

            gflagOn(264);

            stage_SetAnimation(PAL_JPNKOR(323, 319), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKOR(323, 319)) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        }
    }
}

void actSt04rMonyou01Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(274);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        if (GetCharHeldItem(boyGObj) != 1) {
            scpPlayStart(boyGObj);
            scpPlayStart(girlGObj);
        }

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKOR(337, 333), 1, 0);

    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(337, 333), 30, 0) == 0) {
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

void actSt04rMonyou02Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x2000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x2000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(275);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        if (GetCharHeldItem(boyGObj) != 1) {
            scpPlayStart(boyGObj);
            scpPlayStart(girlGObj);
        }

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKOR(338, 334), 1, 0);

    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(338, 334), 30, 0) == 0) {
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

void actSt04rMonyou03Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x3000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(276);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        if (GetCharHeldItem(boyGObj) != 1) {
            scpPlayStart(boyGObj);
            scpPlayStart(girlGObj);
        }

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKOR(339, 335), 1, 0);

    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(339, 335), 30, 0) == 0) {
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

void actSt04rMonyou04Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x4000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x4000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(277);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        if (GetCharHeldItem(boyGObj) != 1) {
            scpPlayStart(boyGObj);
            scpPlayStart(girlGObj);
        }

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKOR(340, 336), 1, 0);

    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(340, 336), 30, 0) == 0) {
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

void actSt04rMonyou05Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x5000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x5000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(278);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        if (GetCharHeldItem(boyGObj) != 1) {
            scpPlayStart(boyGObj);
            scpPlayStart(girlGObj);
        }

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKOR(341, 337), 1, 0);

    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(341, 337), 30, 0) == 0) {
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

void actSt04rMonyou06Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x6000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x6000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(279);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        if (GetCharHeldItem(boyGObj) != 1) {
            scpPlayStart(boyGObj);
            scpPlayStart(girlGObj);
        }

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKOR(342, 338), 1, 0);

    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(342, 338), 30, 0) == 0) {
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

void actSt04rMonyou07Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x7000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x7000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;

    gflagOn(280);

    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        if (GetCharHeldItem(boyGObj) != 1) {
            scpPlayStart(boyGObj);
            scpPlayStart(girlGObj);
        }

        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);

        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKOR(343, 339), 1, 0);

    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(343, 339), 30, 0) == 0) {
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

void actSt04rBarricadeChk(GObj *volatile self)
{
    GObj *n;

    while ((n = scpIsBombExplode(19)) == 0 || scpTriggerBall(self, n, 350.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKOR(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    gflagOn(265);
    _ACTWait(5);

    stage_SetAnimation(PAL_JPNKOR(344, 340), 1, 0);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(PAL_JPNKOR(344, 340)) == 0) {
        if ((pad[0].flags & 0x800) != 0 && scpAdpcmPlayRequestNum() == 0) {
            scpFadeOut(16.0f, 0, 0, 0);

            while (scpFadeChk() != 0) {
                _ACTWait(1);
            }
            while (lt_fade_status() != 2) {
                _ACTWait(1);
            }

            stage_SetAnimation(PAL_JPNKOR(344, 340), 0, -1);

            scpFadeIn(3.0f);
            break;
        }
        _ACTWait(1);
    }

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 3);

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;

    lt_switch_layout(PAL_JPNKOR(54, 51));
}

void actSt04rGondolaChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0xA000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKOR(55, 52));

    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    _ACTWait(15);

    if (gflagChk(282) != 0) {
        stage_SetAnimation(PAL_JPNKOR(229, 225), 1, 200);

        scpWakeupItemWithBoundary(-675.0f, -150.0f, -1600.0f, 200.0f);

        while (stage_CheckAnimationFrame(PAL_JPNKOR(229, 225), 220, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1321, 0, 0, 1);

        while (stage_CheckAnimationFrame(PAL_JPNKOR(229, 225), 240, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1322, 0, 0, 1);

        while (stage_CheckAnimationFrame(PAL_JPNKOR(229, 225), 250, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1319, 0, 0, 1);

        while (stage_CheckAnimationFrame(PAL_JPNKOR(229, 225), 415, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1320, 0, 0, 1);

        while (stage_CheckAnimationFinish(PAL_JPNKOR(229, 225)) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gflagOff(282);
    } else {
        stage_SetAnimation(PAL_JPNKOR(229, 225), 1, 0);

        scpWakeupItemWithBoundary(-675.0f, -1200.0f, -1600.0f, 200.0f);

        SetGirlDangerGObj(boyGObj);

        soundSeDefPlay(1319, 0, 0, 1);

        while (stage_CheckAnimationFrame(PAL_JPNKOR(229, 225), 145, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1320, 0, 0, 1);
        soundSeDefPlay(1321, 0, 0, 1);

        while (stage_CheckAnimationFrame(PAL_JPNKOR(229, 225), 170, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        soundSeDefPlay(1322, 0, 0, 1);

        while (stage_CheckAnimationFrame(PAL_JPNKOR(229, 225), 200, 1) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        ClearGirlDangerGObj();

        gflagOn(282);
    }

    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0xA000000) == 0) {
        gflagOn(286);
    } else {
        gflagOff(286);
    }

    scpBoyControlReadDisable = 0;

    lt_switch_layout(PAL_JPNKOR(54, 51));

    scpWakeupEnemyAll();

    gondola_chk_mes[0].func = actSt04rGondolaCharaChk;
    sub->mail = gondola_chk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04rSolarStageChange(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(234) == 0) {
        solar_stage_change_mes[0].func = actSt04rSolarStageChangeChk;
        act->mail = solar_stage_change_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04rCrest01(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(243) == 0) {
        stage_SetAnimation(PAL_JPNKOR(309, 305), 0, 0);

        crest01_mes[0].func = actSt04rCrestMain;
        act->mail = crest01_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(309, 305), 0, -1);
        scpBoyControlReadDisable = 0;
    }
}

void actSt04rCrest03(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(245) == 0) {
        stage_SetAnimation(PAL_JPNKOR(313, 309), 0, 0);

        crest03_mes[0].func = actSt04rCrest3Main;
        act->mail = crest03_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(313, 309), 0, -1);
        scpBoyControlReadDisable = 0;
    }
}

void actSt04rC1Ball(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(250) == 0) {
        scpSearchGobj(1404)->active = 0;
        scpSearchGobj(1405)->active = 0;

        stage_SetAnimation(PAL_JPNKOR(317, 313), 0, 0);

        c1_ball_mes[0].func = actSt04rC1BallMain;
        act->mail = c1_ball_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(317, 313), 0, -1);
    }
}

void actSt04rC2Ball(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(251) == 0) {
        scpSearchGobj(1487)->active = 0;
        scpSearchGobj(1488)->active = 0;

        stage_SetAnimation(PAL_JPNKOR(318, 314), 0, 0);

        c2_ball_mes[0].func = actSt04rC2BallMain;
        act->mail = c2_ball_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(318, 314), 0, -1);
    }
}

void actSt04rC3Ball(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(252) == 0) {
        scpSearchGobj(1489)->active = 0;
        scpSearchGobj(1490)->active = 0;

        stage_SetAnimation(PAL_JPNKOR(319, 315), 0, 0);

        c3_ball_mes[0].func = actSt04rC3BallMain;
        act->mail = c3_ball_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(319, 315), 0, -1);
    }
}

void actSt04rStair(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(253) == 0) {
        if (girlGObj != 0) {
            stair_mes[0].func = actSt04rStairChk;
            act->mail = stair_mes;
            ACTSendMailCorrect(self, 430);
            _ACTWait(0);
        }
    } else {
        scpSearchGobj(1523)->active = 0;

        stage_SetAnimation(PAL_JPNKOR(351, 347), -1, -2);
    }
}

void actSt04rBrg1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(259) == 0) {
        brg1_mes[0].func = actSt04rBrg1Chk;
        act->mail = brg1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04rBrg2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(260) == 0) {
        brg2_mes[0].func = actSt04rBrg2Chk;
        act->mail = brg2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04rBrg1Way(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(259) == 0) {
        brg1_way_mes[0].func = actSt04rBrg1WayChk;
        act->mail = brg1_way_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04rBrg2Way(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(260) == 0) {
        brg2_way_mes[0].func = actSt04rBrg2WayChk;
        act->mail = brg2_way_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04rRope1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(261) == 0) {
        stage_SetAnimation(PAL_JPNKOR(320, 316), 0, 0);

        rope1_mes[0].func = actSt04rRope1Chk;
        act->mail = rope1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(320, 316), 0, -1);

        scpSearchGobj(1459)->active = 0;
    }
}

void actSt04rRope2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(262) == 0) {
        stage_SetAnimation(PAL_JPNKOR(321, 317), 0, 0);

        rope2_mes[0].func = actSt04rRope2Chk;
        act->mail = rope2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(321, 317), 0, -1);

        scpSearchGobj(1460)->active = 0;
    }
}

void actSt04rRope3(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(263) == 0) {
        stage_SetAnimation(PAL_JPNKOR(322, 318), 0, 0);

        rope3_mes[0].func = actSt04rRope3Chk;
        act->mail = rope3_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(322, 318), 0, -1);

        scpSearchGobj(1461)->active = 0;
    }
}

void actSt04rRope4(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(264) == 0) {
        stage_SetAnimation(PAL_JPNKOR(323, 319), 0, 0);

        rope4_mes[0].func = actSt04rRope4Chk;
        act->mail = rope4_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(323, 319), 0, -1);

        scpSearchGobj(1462)->active = 0;
    }
}

void actSt04rSekizo(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(254) == 0) {
        stage_SetAnimation(PAL_JPNKOR(331, 327), 0, 0);

        sekizo_mes[0].func = actSt04rSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(331, 327), 0, -1);
    }
}

void actSt04rBarricade(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(265) == 0) {
        stage_SetAnimation(PAL_JPNKOR(344, 340), 0, 0);

        barricade_mes[0].func = actSt04rBarricadeChk;
        act->mail = barricade_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(344, 340), 0, -1);
    }
}

void actSt04rGondola(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(282) != 0) {
        stage_SetAnimation(PAL_JPNKOR(229, 225), 0, 200);

        gondola_mes[0].func = actSt04rGondolaChk;
        act->mail = gondola_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(229, 225), 0, 0);

        gondola2_mes[0].func = actSt04rGondolaChk;
        act->mail = gondola2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04rCrest01XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(243) == 0) {
        stage_SetAnimation(PAL_JPNKOR(314, 310), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(314, 310), 0, -1);
    }
}

void actSt04rCrest02XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(244) == 0) {
        stage_SetAnimation(PAL_JPNKOR(315, 311), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(315, 311), 0, -1);
    }
}

void actSt04rCrest03XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(245) == 0) {
        stage_SetAnimation(PAL_JPNKOR(316, 312), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(316, 312), 0, -1);
    }
}

void actSt04rC2BallXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(251) == 0) {
        stage_SetAnimation(PAL_JPNKOR(318, 314), 0, 0);

        scpSearchGobj(1408)->active = 0;
        scpSearchGobj(1409)->active = 0;
    } else {
        stage_SetAnimation(PAL_JPNKOR(318, 314), 0, -1);
    }
}

void actSt04rC3BallXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(252) == 0) {
        stage_SetAnimation(PAL_JPNKOR(319, 315), 0, 0);

        scpSearchGobj(1562)->active = 0;
        scpSearchGobj(1563)->active = 0;
    } else {
        stage_SetAnimation(PAL_JPNKOR(319, 315), 0, -1);
    }
}

void actSt04rTorch1_1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(266) == 0) {
        torch1_1_mes[0].func = actSt04rTorch1_1Chk;
        act->mail = torch1_1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        scpTorchLightOn(1404);
        stage_SetAnimation(PAL_JPNKOR(210, 206), 0, -1);
    }
}

void actSt04rTorch1_2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(267) == 0) {
        torch1_2_mes[0].func = actSt04rTorch1_2Chk;
        act->mail = torch1_2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        scpTorchLightOn(1405);
        stage_SetAnimation(PAL_JPNKOR(211, 207), 0, -1);
    }
}

void actSt04rTorch2_1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(268) != 0 || gflagChk(270) != 0) {
        scpTorchLightOn(1487);
    } else {
        torch2_1_mes[0].func = actSt04rTorch2_1Chk;
        act->mail = torch2_1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04rTorch2_2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(269) != 0 || gflagChk(271) != 0) {
        scpTorchLightOn(1488);
    } else {
        torch2_2_mes[0].func = actSt04rTorch2_2Chk;
        act->mail = torch2_2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04rTorch3_1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(272) == 0) {
        torch3_1_mes[0].func = actSt04rTorch3_1Chk;
        act->mail = torch3_1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        scpTorchLightOn(1489);
    }
}

void actSt04rTorch3_2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(273) == 0) {
        torch3_2_mes[0].func = actSt04rTorch3_2Chk;
        act->mail = torch3_2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        scpTorchLightOn(1490);
    }
}

void actSt04rTorch2_1XL(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(268) != 0 || gflagChk(270) != 0) {
        scpTorchLightOn(1408);
        stage_SetAnimation(PAL_JPNKOR(212, 208), 0, -1);
    } else {
        torch2_1xl_mes[0].func = actSt04rTorch2_1XLChk;
        act->mail = torch2_1xl_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04rTorch2_2XL(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(269) != 0 || gflagChk(271) != 0) {
        scpTorchLightOn(1409);
        stage_SetAnimation(PAL_JPNKOR(213, 209), 0, -1);
    } else {
        torch2_2xl_mes[0].func = actSt04rTorch2_2XLChk;
        act->mail = torch2_2xl_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04rTorch3_1XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(272) != 0) {
        scpTorchLightOn(1562);
    }
}

void actSt04rTorch3_2XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(273) != 0) {
        scpTorchLightOn(1563);
    }
}

void actSt04rMonyou01(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(274) == 0) {
        stage_SetAnimation(PAL_JPNKOR(337, 333), 0, 0);

        monyou01_mes[0].func = actSt04rMonyou01Chk;
        act->mail = monyou01_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(337, 333), 0, -1);
    }
}

void actSt04rMonyou02(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(275) == 0) {
        stage_SetAnimation(PAL_JPNKOR(338, 334), 0, 0);

        monyou02_mes[0].func = actSt04rMonyou02Chk;
        act->mail = monyou02_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(338, 334), 0, -1);
    }
}

void actSt04rMonyou03(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(276) == 0) {
        stage_SetAnimation(PAL_JPNKOR(339, 335), 0, 0);

        monyou03_mes[0].func = actSt04rMonyou03Chk;
        act->mail = monyou03_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(339, 335), 0, -1);
    }
}

void actSt04rMonyou04(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(277) == 0) {
        stage_SetAnimation(PAL_JPNKOR(340, 336), 0, 0);

        monyou04_mes[0].func = actSt04rMonyou04Chk;
        act->mail = monyou04_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(340, 336), 0, -1);
    }
}

void actSt04rMonyou05(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(278) == 0) {
        stage_SetAnimation(PAL_JPNKOR(341, 337), 0, 0);

        monyou05_mes[0].func = actSt04rMonyou05Chk;
        act->mail = monyou05_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(341, 337), 0, -1);
    }
}

void actSt04rMonyou06(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(279) == 0) {
        stage_SetAnimation(PAL_JPNKOR(342, 338), 0, 0);

        monyou06_mes[0].func = actSt04rMonyou06Chk;
        act->mail = monyou06_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(342, 338), 0, -1);
    }
}

void actSt04rMonyou07(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(280) == 0) {
        stage_SetAnimation(PAL_JPNKOR(343, 339), 0, 0);

        monyou07_mes[0].func = actSt04rMonyou07Chk;
        act->mail = monyou07_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKOR(343, 339), 0, -1);
    }
}

void actSt05cDoorInit(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(165) != 0 && gflagChk(234) == 0) {
        stage_SetAnimation(PAL_JPNKOR(347, 343), 0, -1);
    }

    if (gflagChk(165) == 0 || gflagChk(234) != 0) {
        stage_SetAnimation(PAL_JPNKOR(347, 343), 0, 0);
    }
}

void actSt04rC1BallMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = c1_ball_main_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt04rC1BallSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    c1_ball_switch_mes[0].func = actSt04rC1BallTurn;
    sub->mail = c1_ball_switch_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04rC1BallTurn(GObj *volatile self)
{
    turnBall(self, 250, PAL_JPNKOR(317, 313), 1404, 1405);
}

void actSt04rC2BallMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = c2_ball_main_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt04rC2BallSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    c2_ball_switch_mes[0].func = actSt04rC2BallTurn;
    sub->mail = c2_ball_switch_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04rC2BallTurn(GObj *volatile self)
{
    turnBall(self, 251, PAL_JPNKOR(318, 314), 1487, 1488);
}

void actSt04rC3BallMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = c3_ball_main_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt04rC3BallSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    c3_ball_switch_mes[0].func = actSt04rC3BallTurn;
    sub->mail = c3_ball_switch_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04rC3BallTurn(GObj *volatile self)
{
    turnBall(self, 252, PAL_JPNKOR(319, 315), 1489, 1490);
}

void actSt04rCrestMain(GObj *volatile self)
{
    while (scpIsTorchLightOn(1404) == 0 || scpIsTorchLightOn(1405) == 0 || gflagChk(250) == 0) {
        _ACTWait(1);
    }

    gateAnim = PAL_JPNKOR(309, 305);
    gateAdpcm = 27;
    openGate(self);
    _ACTWait(1);

    gflagOn(243);
    gflagOn(246);
    gflagOff(247);
    gflagOff(248);
    gflagOff(249);
    gflagOff(233);
}

void actSt04rSolarStageChangeChk(GObj *volatile self)
{
    while (gflagChk(243) == 0 || gflagChk(244) == 0 || gflagChk(245) == 0 || gflagChk(232) == 0) {
        _ACTWait(1);
    }

    RequestStageChange(8, boyGObj, 0, 2.0f, 8.0f);
}

void actSt04rStairEvent(int x)
{
    volatile int local = x;
}

void actSt04rRope1Event(int x)
{
    volatile int local = x;
}

void actSt04rRope2Event(int x)
{
    volatile int local = x;
}

void actSt04rBrg1Event(int x)
{
    volatile int local = x;
}

void actSt04rBrg1Chk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (gflagChk(261) == 0 || gflagChk(262) == 0) {
        _ACTWait(1);
    }

    gflagOn(259);

    sekizo_4r = PAL_JPNKOR(328, 324);

    brg1_chk_mes[0].func = actSt04rBrgCommon;
    sub->mail = brg1_chk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt04rBrg1Sub(GObj *volatile self)
{
    _ACTWait(60);

    while (sekizo5c == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(sekizo5c->stream);

    stage_SetAnimation(sekizo_4r, 1, 0);

    while (stage_CheckAnimationFrame(sekizo_4r, 75, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFinish(sekizo_4r) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

void actSt04rRope3Event(int x)
{
    volatile int local = x;
}

void actSt04rRope4Event(int x)
{
    volatile int local = x;
}

void actSt04rBrg2Event(int x)
{
    volatile int local = x;
}

void actSt04rBrg2Chk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (gflagChk(263) == 0 || gflagChk(264) == 0) {
        _ACTWait(1);
    }

    gflagOn(260);

    sekizo_4r = PAL_JPNKOR(329, 325);

    brg2_chk_mes[0].func = actSt04rBrgCommon;
    sub->mail = brg2_chk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04rBrg1WayChk(GObj *volatile self)
{
    while (gflagChk(259) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(3, 1);
}

void actSt04rBrg2WayChk(GObj *volatile self)
{
    while (gflagChk(259) == 0 || gflagChk(260) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(4, 1);
}

void actSt04rMonyou01Event(int x)
{
    volatile int local = x;
}

void actSt04rMonyou02Event(int x)
{
    volatile int local = x;
}

void actSt04rMonyou03Event(int x)
{
    volatile int local = x;
}

void actSt04rMonyou04Event(int x)
{
    volatile int local = x;
}

void actSt04rMonyou05Event(int x)
{
    volatile int local = x;
}

void actSt04rMonyou06Event(int x)
{
    volatile int local = x;
}

void actSt04rMonyou07Event(int x)
{
    volatile int local = x;
}

void actSt04rBarricadeEvent(int x)
{
    volatile int local = x;
}

void actSt04rGondolaCharaChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0xA000000) != 0 &&
           (gflagChk(286) == 0 || scpTriggerFloorAttr(girlGObj, 0xA000000) == 0)) {
        _ACTWait(1);
    }

    gondola_chara_chk_mes[0].func = actSt04rGondolaChk;
    sub->mail = gondola_chara_chk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04rTorch1_1Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(1404) == 0) {
        _ACTWait(1);
    }

    gflagOn(266);
    stage_SetAnimation(PAL_JPNKOR(210, 206), 1, 0);
}

void actSt04rTorch1_2Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(1405) == 0) {
        _ACTWait(1);
    }

    gflagOn(267);
    stage_SetAnimation(PAL_JPNKOR(211, 207), 1, 0);
}

void actSt04rTorch2_1Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(1487) == 0) {
        _ACTWait(1);
    }

    gflagOn(268);
}

void actSt04rTorch2_2Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(1488) == 0) {
        _ACTWait(1);
    }

    gflagOn(269);
}

void actSt04rTorch3_1Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(1489) == 0) {
        _ACTWait(1);
    }

    gflagOn(272);
}

void actSt04rTorch3_2Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(1490) == 0) {
        _ACTWait(1);
    }

    gflagOn(273);
}

void actSt04rTorch2_1XLChk(GObj *volatile self)
{
    while (scpIsTorchLightOn(1408) == 0) {
        _ACTWait(1);
    }

    gflagOn(270);
    stage_SetAnimation(PAL_JPNKOR(212, 208), 1, 0);
}

void actSt04rTorch2_2XLChk(GObj *volatile self)
{
    while (scpIsTorchLightOn(1409) == 0) {
        _ACTWait(1);
    }

    gflagOn(271);
    stage_SetAnimation(PAL_JPNKOR(213, 209), 1, 0);
}
