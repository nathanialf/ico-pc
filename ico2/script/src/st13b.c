#include "st13b.h"
#include "StageManager.h"
#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "act.h"
#include "gobj_process.h"
#include "commonact.h"
#include "jimaku.h"
#include "itou_boss.h"
#include "camera-root.h"
#include "fightSound.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "motionManager2.h"
#include <libvu0.h>
#include "e3.h"
#include "typedef.h"
#include "main.h"
#include "script.h"
#include "ico_gamestate.h" /* port: achievement signals, docs/port/ACHIEVEMENTS.md */

static void actSt13bDoorUpSub(GObj *volatile self);
static void actSt13bElev2CharaChk(GObj *volatile self);
static void actSt13bElevUpSub(GObj *volatile self);

/* .sbss: the demo's own end flag, the conte-02 end flag, the pad actuator
   handle and volume byte the door-up subthread holds, and the flag
   actSt13bDoorUpSub raises when the door is up. */
static int demoEnd;

static int conte02End; /* derived name */

static int padAct; /* derived name */

static unsigned char padActVolume; /* derived name */

static int doorUpDone; /* derived name */

/* .data: actor mail packets. */
static ActMail floor_mes[2] = {{430}, {429}}; /* derived name */

static ActMail sekizo_mes[2] = {{430}, {429}}; /* derived name */

static ActMail sekizo2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail meetAgain_mes[2] = {{430}, {429}}; /* derived name */

static ActMail boss_mes[2] = {{430}, {429}}; /* derived name */

static ActMail bossAfter_mes[2] = {{430}, {429}}; /* derived name */

static ActMail elevMain_mes[2] = {{406, actSt13bElevSwitch}, {429}}; /* derived name */

static ActMail elev_mes[2] = {{430}, {429}}; /* derived name */

static ActMail elevSwitch_mes[2] = {{430}, {429}}; /* derived name */

static ActMail elevUp_mes[2] = {{430}, {429}}; /* derived name */

static ActMail doorMain_mes[2] = {{407, actSt13bDoorSwitch}, {429}}; /* derived name */

static ActMail door_mes[2] = {{430}, {429}}; /* derived name */

static ActMail doorSwitch_mes[2] = {{430}, {429}}; /* derived name */

static float doorUpSubPos[4] = {-3434.0f, -200.0f, 0.0f, 0.0f}; /* derived name */

static ActMail elev2Chk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail elev2Chk2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail elev2Chara_mes[2] = {{430}, {429}}; /* derived name */

static ActMail elev2CharaChk_mes[2] = {{430}, {429}}; /* derived name */

/* .sdata: the scene stream, the stream handles and shakes, then a further
   shake and volume. */
static SqEntry *st13b_adpcm = 0; /* derived name */

SqEntry *sekizo13b = 0;

SqEntry *sekizo13b2 = 0;

SqEntry *meets_again = 0;

SqEntry *boss = 0;

SqEntry *sd = 0;

SqEntry *boss_dead = 0;

SqEntry *st13b_up = 0;

SqEntry *st13b_down = 0;

SqEntry *sekizo_13b = 0;

SqEntry *sekizo_13b_vol = 0;

int st13b_yure = 0;

unsigned char st13b_yure_vol = 0;

static int st13b_boss_yure = 0; /* derived name */

static unsigned char st13b_boss_yure_vol = 0; /* derived name */

void actSt13bFloor(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(5) == 0) {
        scpFadeOut(255.0f, 0, 0, 0);
        lt_switch_layout(55);
        scpBoyControlReadDisable = 1;

        stage_SetAnimation(30, 0, 0);
        stage_SetAnimation(26, 0, 0);
        stage_SetAnimation(33, 0, 0);
        stage_SetAnimation(34, 0, 0);
        stage_SetAnimation(36, 0, 0);
        stage_SetAnimation(38, 0, 0);
        stage_SetAnimation(40, 0, 235);
        stage_SetAnimation(43, 0, 0);
        stage_SetAnimation(35, 0, 0);

        floor_mes[0].func = actSt13bFloorChk;
        act->mail = floor_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(32, 0, -1);
        stage_SetAnimation(26, 0, -1);
        stage_SetAnimation(36, 0, 0);
        stage_SetAnimation(38, 0, 0);
    }
}

void actSt13bFloorChk(GObj *volatile self)
{
    int v = 32;

    gflagOn(5);
    actCreateSubThread(actSt13bConte02, 21);
    actCreateSubThread(actSt13bConte02Jimaku, 21);

    conte02End = 0;
    st13b_adpcm = 0;

    do {
        _ACTWait(1);
    } while (st13b_adpcm == 0);

    while (conte02End == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }
    ico_gs_signal(ICO_GS_EV_DEMO_END, 8 + (conte02End == 0));

    if (conte02End == 0) {
        v = 64;
    }

    if (st13b_adpcm != 0) {
        scpAdpcmFadeCloseFunc(&st13b_adpcm, v);
    }

    RequestStageChange(4, boyGObj, 0, 0.025f, 2.0f);
}

void actSt13bConte02(GObj *volatile self)
{
    scpPlayStart(boyGObj);
    stgmgrNextStagePreLoadForceStageSet(0);
    scpAdpcmPlayRequestFunc(11, &st13b_adpcm, 0, 1, 0);

    while (st13b_adpcm == 0) {
        _ACTWait(1);
    }

    scpFadeIn(8.0f);

    stgmgrNextStagePreLoadForceStageSet(exitData[(&stageData[stage_no])->ent[3]].nextStage);
    stgmgrNextStagePreLoadForceNoCancel(1);

    stage_SetAnimation(586, 1, 0);
    scpPlayMot((void *)boyGObj, 282);
    scpPlayMot(scpSearchGobj(2403), 1001);
    scpSearchGobj(2403)->active = 1;
    scpPlayMot(scpSearchGobj(2404), 1023);
    scpSearchGobj(2404)->active = 1;
    scpPlayMot(scpSearchGobj(2405), 1047);
    scpSearchGobj(2405)->active = 1;
    _ACTWait(1);

    stage_SetAnimation(34, 1, 0);
    stage_SetAnimation(21, 1, 0);

    _ACTWait(
        ico_d2i(ico_dmul(ico_i2d((60 - systemStatus[0] * 10) / systemStatus[1]), ICO_D(0.15))));

    if (systemStatus[0] != 0) {
        _ACTWait(ico_d2i(
            ico_i2d((60 - systemStatus[0] * 10) / systemStatus[1]))); /* ee-gcc folded the * 1.0 */
    }

    AdpcmPlay(st13b_adpcm->stream);

    while (stage_ContinueAnimation(586, 587) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 283);
    scpPlayMot(scpSearchGobj(2403), 1002);
    scpPlayMot(scpSearchGobj(2404), 1024);
    scpPlayMot(scpSearchGobj(2405), 1048);
    _ACTWait(1);

    stage_SetAnimation(27, 1, 0);
    stage_SetAnimation(22, 1, 0);

    while (stage_ContinueAnimation(587, 588) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    stage_SetAnimation(28, 1, 0);

    while (stage_ContinueAnimation(588, 589) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 284);
    scpPlayMot(scpSearchGobj(2403), 1003);
    scpPlayMot(scpSearchGobj(2404), 1025);
    scpPlayMot(scpSearchGobj(2405), 1049);
    _ACTWait(1);

    stage_SetAnimation(29, 1, 0);
    stage_SetAnimation(23, 1, 0);

    while (stage_ContinueAnimation(589, 590) == 0) {
        _ACTWait(1);
    }

    scpFadeIn(3.0f);

    scpPlayMot((void *)boyGObj, 285);
    scpPlayMot(scpSearchGobj(2404), 1026);
    _ACTWait(1);

    stage_SetAnimation(30, 1, 0);

    while (stage_ContinueAnimation(590, 591) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 286);
    scpPlayMot(scpSearchGobj(2403), 1002);
    scpPlayMot(scpSearchGobj(2404), 1024);
    scpPlayMot(scpSearchGobj(2405), 1048);
    _ACTWait(1);

    stage_SetAnimation(31, 1, 0);

    while (stage_ContinueAnimation(591, 592) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(2403), 1004);
    scpPlayMot(scpSearchGobj(2404), 1027);
    scpPlayMot(scpSearchGobj(2405), 1050);
    _ACTWait(1);

    stage_SetAnimation(24, 1, 0);

    while (stage_ContinueAnimation(592, 593) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 287);
    scpSearchGobj(2403)->active = 0;
    scpSearchGobj(2404)->active = 0;
    scpSearchGobj(2405)->active = 0;

    stage_SetAnimation(34, 0, 0);

    while (stage_CheckAnimationFrame(593, 200, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpFadeOut(3.0f, 0, 0, 0);

    while (scpFadeChk() != 0) {
        _ACTWait(45);
    }

    stage_SetAnimation(593, -1, -2);
    scpFadeIn(3.0f);
    stage_SetAnimation(594, 1, 0);

    while (stage_ContinueAnimation(594, 595) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 288);
    _ACTWait(1);

    stage_SetAnimation(26, 1, 0);
    _ACTWait(60);

    padAct = iosPadActRequest(boyPad, 9);
    padActVolume = 128;
    iosPadActVolumeSet(padAct, 128);

    while (stage_ContinueAnimation(595, 596) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(596, 597) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(25, 1, 0);

    while (stage_ContinueAnimation(597, 598) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 289);
    _ACTWait(130);

    stage_SetAnimation(32, 1, 0);

    while (stage_ContinueAnimation(598, 599) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(599, 600) == 0) {
        _ACTWait(1);
    }

    iosPadActStop(padAct);
    scpPlayMot((void *)boyGObj, 290);

    while (stage_ContinueAnimation(600, 601) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 288);

    while (stage_ContinueAnimation(601, 602) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 292);
    _ACTWait(1);

    stage_SetAnimation(32, 1, 208);

    while (stage_ContinueAnimation(602, 603) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 293);
    iosPadActRequest(boyPad, 16);

    while (stage_ContinueAnimation(603, 604) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 294);

    _ACTWait(30);
    iosPadActRequest(boyPad, 15);
    _ACTWait(30);
    iosPadActRequest(boyPad, 17);
    _ACTWait(15);
    iosPadActRequest(boyPad, 17);
    _ACTWait(5);
    iosPadActRequest(boyPad, 16);
    _ACTWait(15);
    iosPadActRequest(boyPad, 17);

    while (stage_ContinueAnimation(604, 605) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 295);

    while (stage_ContinueAnimation(605, 606) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 296);

    while (stage_ContinueAnimation(606, 607) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 297);

    while (stage_CheckAnimationFrame(607, 150, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    conte02End = 1;
}

void actSt13bSekizoChk(GObj *volatile self)
{
    float dir[4];

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 || scpGameStat_BoyWeaponkind() != 5 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) == 75) {
        _ACTWait(1);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpAdpcmPlayRequestFunc(19, &sekizo13b, 1, 1, 1);

    while (sekizo13b == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(33, 1, 0);

    st13b_yure = iosPadActRequest(boyPad, 9);
    st13b_yure_vol = 128;
    iosPadActVolumeSet(st13b_yure, 128);

    scpPlayStart(boyGObj);
    scpPlayPosSet((void *)boyGObj, 3085.0f, -1338.0f, 0.0f);
    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    scpSekizouCheckPoint();
    scpPlayMot((void *)boyGObj, 251);
    scpPlayWaitMotEnd(boyGObj);
    scpPlayMot((void *)boyGObj, 0);
    scpPlayEnd(boyGObj);
    gflagOn(10);

    while (stage_CheckAnimationFrame(33, 151, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActStop(st13b_yure);

    while (stage_CheckAnimationFinish(33) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    lt_switch_layout(54);
    scpBoyControlReadDisable = 0;
}

void actSt13bSekizo2Chk(GObj *volatile self)
{
    float dir[4];

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 || scpGameStat_BoyWeaponkind() != 5 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) == 75) {
        _ACTWait(1);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpAdpcmPlayRequestFunc(18, &sekizo13b2, 1, 1, 1);

    while (sekizo13b2 == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(34, 1, 0);

    st13b_yure = iosPadActRequest(boyPad, 9);
    st13b_yure_vol = 128;
    iosPadActVolumeSet(st13b_yure, 128);

    scpPlayStart(boyGObj);
    scpPlayPosSet((void *)boyGObj, -1563.0f, 527.0f, 0.0f);
    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    scpSekizouCheckPoint();
    scpPlayMot((void *)boyGObj, 251);
    scpPlayWaitMotEnd(boyGObj);
    scpPlayMot((void *)boyGObj, 0);
    scpPlayEnd(boyGObj);
    gflagOn(11);

    while (stage_CheckAnimationFrame(34, 151, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActStop(st13b_yure);

    while (stage_CheckAnimationFinish(34) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    lt_switch_layout(54);
    scpBoyControlReadDisable = 0;
}

void actSt13bMeetAgain(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(12) == 0) {
        stage_SetAnimation(32, 0, -1);
        stage_SetAnimation(26, 0, 0);
        stage_SetAnimation(35, 0, 0);
        stage_SetAnimation(36, 0, 0);
        stage_SetAnimation(38, 0, 0);

        meetAgain_mes[0].func = actSt13bMeetAgainChk;
        act->mail = meetAgain_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(32, 0, -1);
        stage_SetAnimation(26, 0, 0);
        stage_SetAnimation(35, 0, 0);
        stage_SetAnimation(36, 0, 0);
        stage_SetAnimation(38, 0, 0);
    }
}

static void actSt13bMeetAgainSub(GObj *volatile self)
{
    stage_SetAnimation(760, 1, 0);

    scpPlayMot((void *)boyGObj, 2);
    scpPlayMot(scpSearchGobj(2464), 971);
    scpPlayMot(scpSearchGobj(2465), 972);
    scpPlayMot(scpSearchGobj(2466), 973);
    scpPlayMot(scpSearchGobj(2467), 974);
    scpPlayMot(scpSearchGobj(2468), 975);
    scpPlayMot(scpSearchGobj(2469), 976);

    while (stage_ContinueAnimation(760, 761) == 0) {
        _ACTWait(1);
    }

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(761) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

void actSt13bMeetAgainChk(GObj *volatile self)
{
    GProc *th;

    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    scpPlayMot((void *)boyGObj, 0);
    gflagOn(12);
    scpAdpcmPlayRequestFunc(35, &meets_again, 1, 1, 1);

    while (meets_again == 0) {
        _ACTWait(1);
    }

    th = actCreateSubThread(actSt13bMeetAgainSub, 21);
    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&th->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }

        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(760, 0, -1);
        scpFadeIn(3.0f);
    }

    scpPlayMot(scpSearchGobj(2464), 977);
    scpPlayMot(scpSearchGobj(2465), 978);
    scpPlayMot(scpSearchGobj(2466), 979);
    scpPlayMot(scpSearchGobj(2467), 980);
    scpPlayMot(scpSearchGobj(2468), 981);
    scpPlayMot(scpSearchGobj(2469), 982);

    scpPlayMot((void *)boyGObj, 0);
    scpPlayEnd(boyGObj);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(54);
    scpAdpcmFadeCloseFunc(&meets_again, 128);
}

void actSt13bBoss(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    ScpCallCameraSetTarget(-1886.0f, 625.0f, -4.0f);
    fightSoundProcessRequestPause();

    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    if (gflagChk(13) == 0) {
        scpSearchGobj(2470)->active = 0;
        boss_mes[0].func = actSt13bBossChk;
        act->mail = boss_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else if (gflagChk(14) == 0) {
        scpAdpcmPlayRequestFunc(37, &boss, 1, 0, 1);
        scpSeEnvMasterVolRate = 0.5f;
    }
}

void actSt13bBossChk(GObj *volatile self)
{
    while (scpTriggerFloorAttr(boyGObj, 0x2000000) == 0) {
        _ACTWait(1);
    }

    gflagOn(13);
    scpAdpcmPlayRequestFunc(36, &sd, 1, 1, 1);

    while (sd == 0) {
        _ACTWait(1);
    }

    scpAdpcmPlayRequestFunc(37, &boss, 1, 0, 0);
    scpSeEnvMasterVolRate = 0.5f;

    scpSearchGobj(2470)->active = 1;
    scpPlayPosSet(scpSearchGobj(2470), 0.0f, -100.0f, 0.0f);

    scpPlayMot(scpSearchGobj(2464), 834);
    scpPlayMot(scpSearchGobj(2465), 834);
    scpPlayMot(scpSearchGobj(2466), 834);
    scpPlayMot(scpSearchGobj(2467), 834);
    scpPlayMot(scpSearchGobj(2468), 834);
    scpPlayMot(scpSearchGobj(2469), 834);

    CapsuleGhostBossStart();

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 6);

    while (boss == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(boss->stream);
}

void actSt13bBossAfterChk(GObj *volatile self)
{
    GProc *th;

    while (InqCapsuleGhostBossEnd() == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    gflagOn(14);
    scpAdpcmFadeCloseFunc(&boss, 128);

    while (scpAdpcmCloseChkFunc(&boss) != 0) {
        _ACTWait(1);
    }

    scpAdpcmPlayRequestFunc(38, &boss_dead, 1, 1, 1);

    while (boss_dead == 0) {
        _ACTWait(1);
    }

    th = actCreateSubThread(actConte10c, 21);
    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&th->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);
        scpAdpcmFadeCloseFunc(&boss_dead, 512);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }

        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(763, 0, -1);
        stage_SetAnimation(35, 0, -1);
        scpFadeIn(3.0f);
    }

    scpPlayMot((void *)boyGObj, 0);
    scpPlayEnd(boyGObj);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(54);
}

static void actSt13bElevDownSub(GObj *volatile self)
{
    stage_SetAnimation(40, 1, 235);

    while (stage_CheckAnimationFrame(40, 321, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 17);

    while (stage_CheckAnimationFrame(40, 350, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    st13b_boss_yure = iosPadActRequest(boyPad, 9);
    st13b_boss_yure_vol = 128;
    iosPadActVolumeSet(st13b_boss_yure, 128);

    while (stage_CheckAnimationFrame(40, 450, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

void actSt13bElevDown(GObj *volatile self)
{
    GProc *th;

    lt_switch_layout(55);
    gflagOn(15);
    scpAdpcmPlayRequestFunc(79, &st13b_down, 0, 1, 1);

    while (st13b_down == 0) {
        _ACTWait(1);
    }

    preload(3);
    st13b_boss_yure = -1;
    th = actCreateSubThread(actSt13bElevDownSub, 21);
    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&th->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);
        scpAdpcmFadeCloseFunc(&st13b_down, 512);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }

        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }
    }

    if (st13b_boss_yure >= 0) {
        iosPadActStop(st13b_boss_yure);
    }

    RequestStageChange(3, boyGObj, 0, 2.0f, 4.0f);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(54);
}

void actSt13bElevUp(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(325) != 0) {
        lt_switch_layout(55);
        scpBoyControlReadDisable = 1;
        scpFadeOut(255.0f, 0, 0, 0);
        stage_SetAnimation(40, 0, 235);
        _ACTWait(10);
        scpBoyControlReadDisable = 1;
        stage_SetAnimation(40, 0, 0);
        elevUp_mes[0].func = actSt13bElevUpChk;
        act->mail = elevUp_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(40, 0, 235);
    }
}

void actSt13bElevUpChk(GObj *volatile self)
{
    GProc *th;

    scpAdpcmPlayRequestFunc(78, &st13b_up, 1, 1, 1);

    while (st13b_up == 0) {
        _ACTWait(1);
    }

    scpFadeIn(6.0f);
    th = actCreateSubThread(actSt13bElevUpSub, 21);

    demoEnd = 0;
    st13b_boss_yure = -1;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&th->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);
        scpAdpcmFadeCloseFunc(&st13b_up, 512);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }

        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(40, 0, 235);
        _ACTWait(2);
        scpPlayPosSet((void *)boyGObj, -772.0f, 527.0f, -226.0f);

        if (st13b_boss_yure >= 0) {
            iosPadActStop(st13b_boss_yure);
        }

        scpFadeIn(3.0f);
    }

    scpBoyControlReadDisable = 0;
    lt_switch_layout(54);
    gflagOff(325);
}

void actSt13bDoorUp(GObj *volatile self)
{
    GProc *th;

    lt_switch_layout(55);
    gflagOn(17);
    th = actCreateSubThread(actSt13bDoorUpSub, 21);

    demoEnd = 0;
    doorUpDone = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&th->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }

        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(43, 0, -1);
        scpFadeIn(3.0f);
    }

    if (doorUpDone == 0) {
        soundSeDefPlay(1222, 0, 0, 1);
    }

    scpBoyControlReadDisable = 0;
    lt_switch_layout(54);
}

static void actSt13bElev2Chk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0x4000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;

    if (gflagChk(16) != 0) {
        scpAdpcmPlayRequestFunc(100, &sekizo_13b_vol, 1, 1, 1);

        while (sekizo_13b_vol == 0) {
            _ACTWait(1);
        }

        _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1]);
        stage_SetAnimation(42, 1, 0);

        while (stage_CheckAnimationFrame(42, 50, 0) == 0) {
            _ACTWait(1);
        }

        _ACTWait(1);
        iosPadActRequest(boyPad, 16);

        while (stage_CheckAnimationFinish(42) == 0) {
            _ACTWait(1);
        }

        _ACTWait(1);
        gflagOff(16);
        scpBoyControlReadDisable = 0;
    } else {
        scpAdpcmPlayRequestFunc(99, &sekizo_13b, 0, 1, 1);

        while (sekizo_13b == 0) {
            _ACTWait(1);
        }

        _ACTWait(
            ico_d2i(ico_dmul(ico_i2d((60 - systemStatus[0] * 10) / systemStatus[1]), ICO_D(0.15))));
        stage_SetAnimation(41, 1, 0);

        while (stage_CheckAnimationFinish(41) == 0) {
            _ACTWait(1);
        }

        _ACTWait(1);
        gflagOn(16);
        RequestStageChange(2, boyGObj, 0, 2.0f, 8.0f);
        scpBoyControlReadDisable = 0;
    }

    lt_switch_layout(54);

    elev2Chara_mes[0].func = actSt13bElev2CharaChk;
    sub->mail = elev2Chara_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt13bSekizo(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(10) == 0) {
        stage_SetAnimation(33, 0, 0);

        sekizo_mes[0].func = actSt13bSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(33, 0, -1);
    }
}

void actSt13bSekizo2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(11) == 0) {
        stage_SetAnimation(34, 0, 0);

        sekizo2_mes[0].func = actSt13bSekizo2Chk;
        act->mail = sekizo2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(34, 0, -1);
    }
}

void actSt13bBossAfter(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(14) == 0) {
        stage_SetAnimation(35, 0, 0);

        bossAfter_mes[0].func = actSt13bBossAfterChk;
        act->mail = bossAfter_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(35, 0, -1);
    }
}

void actSt13bStoneGirl(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (current_stage_no == 46) {
        scpPlayMot(scpSearchGobj(2462), 808);
        scpSearchGobj(2462)->active = 1;
    }
}

void actSt13bElev(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    elev_mes[0].func = actSt13bElevMain;
    act->mail = elev_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt13bElev2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(16) != 0) {
        stage_SetAnimation(42, 0, 0);

        elev2Chk_mes[0].func = actSt13bElev2Chk;
        act->mail = elev2Chk_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(41, 0, 0);

        elev2Chk2_mes[0].func = actSt13bElev2Chk;
        act->mail = elev2Chk2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt13bDoor(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(17) == 0) {
        stage_SetAnimation(43, 0, 0);

        door_mes[0].func = actSt13bDoorMain;
        act->mail = door_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(43, 0, -1);
    }
}

void actBossTest(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    stage_SetAnimation(32, 0, -1);
    stage_SetAnimation(26, 0, 0);
    stage_SetAnimation(35, 0, 0);
    stage_SetAnimation(36, 0, 0);
    stage_SetAnimation(38, 0, 0);
}

void actSt13bConte02Jimaku(GObj *volatile self)
{
    float t;
    float tn;
    int n;

    t = 0.0f;
    do {
        switch ((int)t) {
        case 1:
            jimakuBegin(&jimaku_msg);
            break;
        case 1800:
            jimaku_msg.sub.block = 1;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        }

        n = (int)t;
        tn = t + (float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f;
        if (n != (int)tn) {
            _ACTWait(1);
            t = tn;
        } else {
            t = tn + 1.0f;
        }
    } while (t < 2500.0f);
}

void actSt13bSekizo2Event(int x)
{
    volatile int local = x;
}

void actConte10c(GObj *volatile self)
{
    stage_SetAnimation(763, 1, 0);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFrame(763, 300, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    stage_SetAnimation(35, 1, 0);

    while (stage_CheckAnimationFinish(763) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

void actSt13bElevMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = elevMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt13bElevSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    scpBoyControlReadDisable = 1;

    elevSwitch_mes[0].func = actSt13bElevDown;
    sub->mail = elevSwitch_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt13bElevUpSub(GObj *volatile self)
{
    stage_SetAnimation(40, 1, 0);
    st13b_boss_yure = iosPadActRequest(boyPad, 9);
    st13b_boss_yure_vol = 128;
    iosPadActVolumeSet(st13b_boss_yure, 128);
    while (stage_CheckAnimationFrame(40, 200, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 17);
    iosPadActStop(st13b_boss_yure);
    while (stage_CheckAnimationFrame(40, 234, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

void actSt13bDoorMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = doorMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt13bDoorSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    scpBoyControlReadDisable = 1;

    doorSwitch_mes[0].func = actSt13bDoorUp;
    sub->mail = doorSwitch_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt13bDoorUpSub(GObj *volatile self)
{
    _ACTWait(60);

    stage_SetAnimation(43, 1, 0);

    _ACTWait(120);

    soundSeDefPlay(1221, 0, doorUpSubPos, 1);

    _ACTWait(30);

    doorUpDone = 1;
    soundSeDefPlay(1222, 0, doorUpSubPos, 1);

    while (stage_CheckAnimationFinish(43) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

static void actSt13bElev2CharaChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0x4000000) != 0) {
        _ACTWait(1);
    }

    elev2CharaChk_mes[0].func = actSt13bElev2Chk;
    sub->mail = elev2CharaChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}
