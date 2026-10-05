#include "StageManager.h"
#include "debug.h"
#include "gamesys.h"
#include "layout_texture.h"
#include "sceneManager.h"
#include "pad.h"
#include "thread.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "jimaku.h"
#include "way_llf.h"
#include "brain.h"
#include "generator.h"
#include "camera-root.h"
#include "fightSound.h"
#include "gflag.h"
#include "RegistPacket.h"
#include "StageAnimation.h"
#include "motionManager2.h"
#include "staticBlur.h"
#include "windManager.h"
#include <libvu0.h>
#include <string.h>
#include "act.h"
#include "gobj_process.h"
#include "typedef.h"
#include "commonact.h"
#include "streamMotionManager.h"
#include "kanbanBoot.h"
#include "layout_action.h"
#include "script.h"
#include "main.h"

void actE3WarningChk(GObj *volatile self)
{
    GObj *x = self;
    int i;

    actInitialize(self);
    _ACTWait(1);

    stgmgrNextStagePreLoadForceStageSet(95);

    i = 0;
    while (i++ < (60 - systemStatus[0] * 10) / systemStatus[1] * 5) {
        _ACTWait(1);
    }

    scpFadeOut(6.0f, 0, 0, 0);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    _ACTWait((int)(float)((60 - systemStatus[0] * 10) / systemStatus[1]));

    RequestStageChange(1, boyGObj, 0, 255.0f, 0.0f);
}

#include "e3.h"

static inline void actE3St09aBrgSwitch(GObj *volatile self);

/* .sdata: the title stream's handle, the capsule's, the cage fall's, the
   first gate's, the stone statue's and its volume word, then the statue
   shake's volume. */
static SqEntry *e3title = 0; /* derived name */

SqEntry *e3capsule = 0;

static SqEntry *e3cage = 0; /* derived name */

SqEntry *e3gate1st = 0;

SqEntry *sekizo_e3 = 0;

int sekizo_e3_vol = 0;

static unsigned char e3sekizo_yure_vol = 0; /* derived name */

static ActMail title_mes[2] = {{430}, {429}}; /* derived name */

static ActMail inst1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail capsule_mes[2] = {{430}, {429}}; /* derived name */

static ActMail doorMain_mes[2] = {{406, actE3DoorSwitch}, {429}}; /* derived name */

static ActMail door_mes[2] = {{430}, {429}}; /* derived name */

static ActMail doorSwitch_mes[2] = {{430}, {429}}; /* derived name */

static float doorUpPos[4] = {-3434.0f, -200.0f, 0.0f, 0.0f}; /* derived name */

static ActMail st13cIntro_mes[2] = {{430}, {429}}; /* derived name */

static ActMail cageFallReady_mes[2] = {{430}, {429}}; /* derived name */

static ActMail cageFall_mes[2] = {{430}, {429}}; /* derived name */

static ActMail st01bEne_mes[2] = {{430}, {429}}; /* derived name */

static float st09aSekizoPos[4] = {1548.0f, -412.0f, -608.0f, 0.0f}; /* derived name */

static ActMail st09aSekizo_mes[2] = {{430}, {429}}; /* derived name */

static ActMail gate_mes[2] = {{430}, {429}}; /* derived name */

static ActMail st09aBrgMain_mes[2] = {{407, actE3St09aBrgSwitch}, {429}}; /* derived name */

static ActMail st09aBrg_mes[2] = {{430}, {429}}; /* derived name */

static ActMail st09aBrgSwitch_mes[2] = {{430}, {429}}; /* derived name */

void actE3Title(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    enable_game_pause = 0;
    scpBoyControlReadDisable = 1;

    gflagOff(356);

    e3title = 0;
    scpAdpcmPlayRequestFunc(5, &e3title, 0, 1, 0);

    while (kanbanBootEnd == 0) {
        _ACTWait(1);
    }

    systemStatus[11] = 7;

    scpFadeOut(255.0f, 0, 0, 0);

    while (e3title == 0) {
        _ACTWait(1);
    }

    scpFadeIn(6.0f);

    if ((ICO_WORD)boyGObj != 0) {
        scpPlayMot(boyGObj, 0);
    }

    stage_SetAnimation(32, 0, 0);
    stage_SetAnimation(26, 0, -1);
    stage_SetAnimation(35, 0, 0);
    stage_SetAnimation(33, 0, 0);
    stage_SetAnimation(36, 0, 0);
    stage_SetAnimation(38, 0, 0);
    stage_SetAnimation(43, 0, 0);

    title_mes[0].func = actE3TitleChk;
    sub->mail = title_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actE3TitleChk(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    actCreateSubThread(actE3TitleFrameChk, 21);

    while (gflagChk(356) != 0 || (pad[0].flags & 0x840) == 0) {
        _ACTWait(1);
    }

    gflagOn(357);
    debug_StdPrintfDummy("game_start\n");

    AdpcmPlay(e3title->stream);

    scpFadeOut(4.0f, 0, 0, 0);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    RequestStageChange(1, boyGObj, 0, 255.0f, 16.0f);
}

void actE3TitleFrameChk(GObj *volatile self)
{
    int intro = 943;
    int loop = 938;
    int outro = 948;
    int i;

    stage_SetAnimation(935, 1, 0);

    switch (NonLinearCameraMove) {
    case 3:
        intro = 944;
        break;
    case 4:
        intro = 945;
        break;
    case 6:
        intro = 947;
        break;
    case 5:
        intro = 946;
        break;
    }
    stage_SetAnimation(intro, 1, 0);

    while (stage_ContinueAnimation(935, 936) == 0) {
        _ACTWait(1);
    }

    stage_SetLoopFlag(936, 1);

    switch (NonLinearCameraMove) {
    case 3:
        loop = 939;
        break;
    case 4:
        loop = 940;
        break;
    case 6:
        loop = 942;
        break;
    case 5:
        loop = 941;
        break;
    }
    stage_SetLoopFlag(loop, 1);
    stage_SetAnimation(loop, 1, 0);

    i = 0;
    while (i++ < (60 - systemStatus[0] * 10) / systemStatus[1] * 45) {
        _ACTWait(1);
    }

    stage_SetAnimation(937, 1, 0);

    switch (NonLinearCameraMove) {
    case 3:
        outro = 949;
        break;
    case 4:
        outro = 950;
        break;
    case 6:
        outro = 952;
        break;
    case 5:
        outro = 951;
        break;
    }
    stage_SetAnimation(outro, 1, 0);

    while (stage_CheckAnimationFinish(937) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (gflagChk(357) == 0) {
        debug_StdPrintfDummy("mpeg_start\n");

        gflagOn(356);

        if (e3title != 0) {
            scpAdpcmCloseFunc(&e3title);
        }

        mpegPlayReturnStage = 95;
        stage_after_skipping_demo = 95;

        stgmgrForceSwitchWithFade(103, 4.0f, 4.0f);
    }
}

void actE3Inst1Chk(GObj *volatile self)
{
    GObj *x = self;
    int anim;
    int i;

    actInitialize(self);
    _ACTWait(1);

    anim = 960;
    switch (NonLinearCameraMove) {
    case 3:
        anim = 961;
        break;
    case 4:
        anim = 962;
        break;
    case 6:
        anim = 964;
        break;
    case 5:
        anim = 963;
        break;
    }

    stage_SetLoopFlag(anim, 1);
    stage_SetAnimation(anim, 1, 0);

    i = 0;
    while (i++ < (60 - systemStatus[0] * 10) / systemStatus[1] * 10 &&
           (pad[0].flags & 0x840) == 0) {
        _ACTWait(1);
    }

    soundSeDefPlay(1390, 0, 0, 1);

    scpFadeOut(3.0f, 0, 0, 0);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    RequestStageChange(1, boyGObj, 0, 255.0f, 16.0f);
}

void actE3Capsule(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if ((ICO_WORD)girlGObj == 0) {
        ScpCallCameraOff();
    }

    if (gflagChk(358) == 0) {
        if ((ICO_WORD)boyGObj != 0) {
            scpPlayMot(boyGObj, 0);
        }

        scpFadeOut(255.0f, 0, 0, 0);

        lt_switch_layout(55);

        stage_SetAnimation(32, 0, 0);
        stage_SetAnimation(26, 0, -1);
        stage_SetAnimation(35, 0, 0);
        stage_SetAnimation(33, 0, 0);
        stage_SetAnimation(36, 0, 0);
        stage_SetAnimation(38, 0, 0);

        capsule_mes[0].func = actE3CapsuleChk;
        sub->mail = capsule_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(32, 0, -1);
        stage_SetAnimation(26, 0, -1);
        stage_SetAnimation(35, 0, 0);
        stage_SetAnimation(33, 0, 0);
        stage_SetAnimation(36, 0, 0);
        stage_SetAnimation(38, 0, 0);
    }
}

/* .sbss: the capsule demo's sub-thread and the flag that thread raises when
   the demo has finished. */
static GProc *capsuleDemoThread; /* derived name */

static int demoEnd;

void actE3CapsuleDemoCancel(GObj *volatile self)
{
    while (demoEnd == 0 || (pad[0].flags & 0x800) == 0) {
        _ACTWait(1);
    }

    iosThreadSetPri(&capsuleDemoThread->thread, 34);

    scpFadeOut(8.0f, 0, 0, 0);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    scpAdpcmCloseFunc(&e3capsule);

    stage_SetAnimation(32, 0, -1);
    stage_SetAnimation(26, 0, -1);
    stage_SetAnimation(594, -1, -2);
    stage_SetAnimation(599, -1, -2);
    stage_SetAnimation(601, -1, -2);
    stage_SetAnimation(602, -1, -2);
    stage_SetAnimation(603, -1, -2);
    stage_SetAnimation(604, -1, -2);
    stage_SetAnimation(605, -1, -2);
    stage_SetAnimation(606, -1, -2);
    stage_SetAnimation(624, 1, 0);

    scpFadeIn(6.0f);

    actCreateSubThread(actE3CapsuleDemoEnd, 21);
}

void actE3CapsuleDemo(GObj *volatile self)
{
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);

    demoEnd = 1;
    actCreateSubThread(actE3CapsuleDemoCancel, 21);

    stage_SetAnimation(594, 1, 0);
    _ACTWait(95);
    stage_SetAnimation(32, 1, 0);

    while (stage_ContinueAnimation(594, 599) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 288);

    while (stage_ContinueAnimation(599, 601) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 288);

    while (stage_ContinueAnimation(601, 602) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 292);

    _ACTWait(1);

    stage_SetAnimation(32, 1, 208);

    while (stage_ContinueAnimation(602, 603) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 293);

    while (stage_ContinueAnimation(603, 604) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 294);

    while (stage_ContinueAnimation(604, 605) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 295);

    while (stage_ContinueAnimation(605, 606) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 296);

    while (stage_ContinueAnimation(606, 624) == 0) {
        _ACTWait(1);
    }

    demoEnd = 0;
    actCreateSubThread(actE3CapsuleDemoEnd, 21);
}

void actE3St13cInit(void)
{
    SetWayGroupActive(7, 0);

    if (gflagChk(360) != 0) {
        SetWayGroupActive(9, 0);
    }
}

void actE3CageFall(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (gflagChk(360) == 0) {
        stage_SetAnimation(76, 0, 0);
        stage_SetAnimation(72, 0, -1);

        if ((ICO_WORD)girlGObj == 0) {
            _ACTWait(0);
        }

        scpPlayPosSet(girlGObj, -30.0f, -436.0f, -1.0f);

        cageFall_mes[0].func = actE3CageFallChk;
        sub->mail = cageFall_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(76, 0, 0);
        stage_SetAnimation(74, 0, -1);
        stage_SetAnimation(75, 0, -1);
    }
}

void actE3CageFallChk(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 160.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);

    scpBoyControlReadDisable = 1;

    ((Act *)scpSearchGobj(3229)->act)->flags20.ll |= 0x20000;
    ((Act *)scpSearchGobj(3230)->act)->flags20.ll |= 0x20000;

    fightSoundProcessRequestPause();
    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    gflagOn(360);

    while (e3cage == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(e3cage->stream);

    actCreateSubThread(actE3CageFallDemo, 21);

    stage_SetAnimation(74, 1, 0);
    stage_SetAnimation(75, 1, 0);

    actCreateSubThread(actE3CageFallEffect, 21);

    while (stage_CheckAnimationFinish(74) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    SetWayGroupActive(9, 0);
}

void actE3CageFallDemo(GObj *volatile self)
{
    float dir[4];

    lt_switch_layout(55);

    scpBoyControlReadDisable = 1;

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    stage_SetAnimation(630, 1, 0);

    scpPlayMot(boyGObj, 315);

    while (stage_ContinueAnimation(630, 631) == 0) {
        _ACTWait(1);
    }

    SetHandCameraLimitInDemo(0, 0);

    while (stage_ContinueAnimation(631, 632) == 0) {
        _ACTWait(1);
    }

    ResetHandCameraLimitInDemo();

    scpPlayMot(girlGObj, 726);

    while (stage_ContinueAnimation(632, 633) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 727);

    while (stage_ContinueAnimation(633, 634) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 316);

    while (stage_ContinueAnimation(634, 635) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 325);

    while (stage_ContinueAnimation(635, 636) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 728);

    while (stage_ContinueAnimation(636, 637) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 729);

    _ACTWait(240);

    gflagOn(364);

    fightSoundProcessRequestStart();

    while (stage_ContinueAnimation(637, 955) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 323);

    while (stage_ContinueAnimation(955, 956) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 326);

    while (stage_CheckAnimationFrame(956, 150, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 324);

    while (stage_CheckAnimationFinish(956) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpPlayEnd(boyGObj);

    scpBoyControlReadDisable = 0;

    lt_switch_layout(54);

    scpPlayMot(girlGObj, 532);
    {
        GObj *obj = girlGObj;

        GOBJ_SUB(obj)->ctrl.blendFrames =
            (int)((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f * 30.0f);

        scpPlayEnd(obj);
    }

    gflagOn(381);

    gamesysObjInfoPosSetStage(boyGObj, GOBJ_ACT(boyGObj)->infoPos, 0, stage_no);

    CheckPoint();

    gflagOff(381);
}

inline void actE3CapsuleDemoEnd(GObj *volatile self)
{
    scpPlayMot(boyGObj, 307);

    while (stage_CheckAnimationFinish(624) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);

    {
        EffectArg ofs = {{-1000.0f, 0.0f, -2200.0f, 1.0f}};
        float dir[4];

        sceVu0SubVector(dir, &ofs, test_CURRENTROOT(boyGObj));
        scpPlayMotDir(boyGObj, dir);
    }

    scpPlayEnd(boyGObj);

    scpBoyControlReadDisable = 0;

    lt_switch_layout(54);

    enable_game_pause = 1;
}

void actE3CageFallEffect(GObj *volatile self)
{
    float t;
    float tn;
    int n;

    t = 0.0f;
    do {
        switch ((int)t) {
        case 0:
            gflagOn(22);
            break;
        case 64:
            iosPadActRequest(boyPad, 17);
            {
                EffectArg b1 = {{-88.0f, -50.0f, -1.0f, 1.0f}};
                scpEffectStart(&b1, 0);
            }
            {
                EffectArg b2 = {{-96.0f, -45.0f, 34.0f, 1.0f}};
                scpEffectStart(&b2, 0);
            }
            break;
        case 68: {
            EffectArg b3 = {{72.0f, -50.0f, 8.0f, 1.0f}};
            scpEffectStart(&b3, 0);
        }
            {
                EffectArg b4 = {{80.0f, -50.0f, 2.0f, 1.0f}};
                scpEffectStart(&b4, 0);
            }
            break;
        case 96: {
            EffectArg b5 = {{-27.0f, -50.0f, 100.0f, 1.0f}};
            scpEffectStart(&b5, 0);
        }
            {
                EffectArg b6 = {{-56.0f, -50.0f, 66.0f, 1.0f}};
                scpEffectStart(&b6, 0);
            }
            {
                EffectArg b7 = {{-5.0f, -50.0f, 42.0f, 1.0f}};
                scpEffectStart(&b7, 0);
            }
            break;
        case 180:
            iosPadActRequest(boyPad, 15);
            break;
        case 300: {
            EffectArg b8 = {{-10.0f, 0.0f, 466.0f, 1.0f}};
            scpEffectStart(&b8, 0);
        }
            {
                EffectArg b9 = {{-25.0f, 0.0f, 450.0f, 1.0f}};
                scpEffectStart(&b9, 0);
            }
            break;
        case 380:
            iosPadActRequest(boyPad, 16);
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
    } while (t < 400.0f);
}

void actE3St09aSekizo(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    soundSeDefPlay(1346, 0, st09aSekizoPos, 1);
    soundSeDefPlay(1347, 0, st09aSekizoPos, 1);
    soundSeDefPlay(1348, 0, st09aSekizoPos, 1);

    if (gflagChk(366) == 0) {
        stage_SetAnimation(376, 0, 0);

        st09aSekizo_mes[0].func = actE3St09aSekizoChk;
        sub->mail = st09aSekizo_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        ScpCallCameraSetTarget(1421.0f, 97.0f, -1885.0f);
        stage_SetAnimation(376, 0, -1);
    }
}

inline void actE3St09aGirlWay(GObj *volatile self)
{
    EffectArg buf = {{-1410.0f, -100.0f, 1950.0f, 0.0f}};
    long long way[2];

    _SCPMoveCharactorByWay(girlGObj, 0, buf.f, 100.0f, 0);

    memset(way, 0, 16);
    RequestStageChangeDirect(girlGObj, 102, way, 180);

    lt_switch_layout(54);

    scpBoyControlReadDisable = 0;
    brainUnlockGirl();
}

void actE3St09aSekizoChk(GObj *volatile self)
{
    float dir[4];

    if ((ICO_WORD)girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) == 75) {
        _ACTWait(1);
    }

    scpSekizouCheckPoint();

    scpAdpcmPlayRequestFunc(18, 0, 1, 1, 1);

    scpKillEnemyAll();
    scpMaskGeneratorAll();

    lt_switch_layout(55);

    scpBoyControlReadDisable = 1;
    brainLockGirl();

    gflagOn(366);

    _ACTWait(60);

    stage_SetAnimation(376, 1, 0);

    sekizo_e3_vol = iosPadActRequest(boyPad, 9);
    e3sekizo_yure_vol = 128;
    iosPadActVolumeSet(sekizo_e3_vol, 128);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayPosSet(girlGObj, -1410.0f, -100.0f, 1515.0f);
    scpPlayPosSet(boyGObj, -1330.0f, -100.0f, 1515.0f);

    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpBoyControlReadDisable = 1;

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpPlayMot(girlGObj, 645);
    scpPlayWaitMotEnd(girlGObj);

    while (stage_CheckAnimationFrame(376, 151, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(sekizo_e3_vol);

    scpPlayMot(girlGObj, 532);
    scpPlayEnd(girlGObj);

    actCreateSubThread(actE3St09aGirlWay, 21);

    _ACTWait(30);

    scpPlayMot(boyGObj, 252);
    scpPlayWaitMotEnd(boyGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    ScpCallCameraSetTarget(1421.0f, 97.0f, -1885.0f);
}

/* stream-motion-def's table; streamMotionManager.h, which defines the
   record, does not declare it */
extern StreamMotionFile streamMotion[];

void actE3GateChk(GObj *volatile self)
{
    int i;

    if ((ICO_WORD)girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 3000.0f) == 0) {
        _ACTWait(1);
    }

    StandbyStreamMotion(streamMotion[9].path);

    i = 0;
    while (CheckReadyStreamMotion() == 0) {
        i++;
        debug_StdPrintfDummy("Now waiting for standby stream motion system... %d\n", i);
        _ACTWait(1);
    }

    lt_switch_layout(55);

    SetWindManager(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 5.0f, 0.0f);

    scpPlayStart(girlGObj);

    reg_SetScissorSw(1);
    SetStaticBlur(0);

    scpAdpcmPlayRequestFunc(4, &e3gate1st, 1, 1, 0);
    while (e3gate1st == 0) {
        _ACTWait(1);
    }

    gflagOn(361);
    _ACTWait(1);

    actCreateSubThread(actE3GateDemo, 21);
    actCreateSubThread(actE3GateJimaku, 21);

    stage_SetAnimation(269, 1, 0);
}

void actE3GateDemo(GObj *volatile self)
{
    scpSearchGobj(3382)->active = 1;

    EntryStreamMotion(boyGObj);
    EntryStreamMotion(girlGObj);
    EntryStreamMotion(scpSearchGobj(3382));

    PlayStreamMotion();

    scpFadeIn(6.0f);

    stage_SetAnimation(648, 1, 0);

    while (stage_CheckAnimationFrame(648, 10, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    AdpcmPlay(e3gate1st->stream);

    while (stage_ContinueAnimation(648, 649) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(649, 650) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(650, 651) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(651, 652) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(652, 653) == 0) {
        _ACTWait(1);
    }

    reg_SetScissorSw(0);

    while (stage_ContinueAnimation(653, 654) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(654, 655) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(655, 656) == 0) {
        _ACTWait(1);
    }

    SetStaticBlur(8);
    SetAuraInspireParam(100.0f);

    stage_SetAnimation(475, 1, 0);

    iosPadActRequest(boyPad, 15);

    soundSeDefPlay(1425, 0, 0, 0);

    while (stage_CheckAnimationFrame(656, 45, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (e3gate1st != 0) {
        scpAdpcmFadeCloseFunc(&e3gate1st, 80);
    }

    ReinitWindManager();

    lt_switch_layout(54);

    enable_game_pause = 0;

    scpFadeOut(4.0f, 255, 255, 255);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    while (scpSeEnvMasterVolRate > 0.0f) {
        scpSeEnvMasterVolRate -= 0.012f;
        _ACTWait(1);
    }

    mpegPlayReturnStage = 95;

    stgmgrForceSwitchWithFadeColor(104, 255.0f, 4.0f, 0, 0, 0);
}

void actE3GateJimaku(GObj *volatile self)
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
        case 300:
            jimaku_msg.sub.block = 25;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 1420:
            jimaku_msg.sub.block = 26;
            jimaku_msg.sub.jump = 30;
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
    } while (t < 7300.0f);
}

void actE3St01bInit(void)
{
    stage_SetAnimation(183, 0, -1);
}

static void actE3St09aBrgDown(GObj *volatile self)
{
    lt_switch_layout(55);
    gflagOn(86);

    scpAdpcmPlayRequestFunc(90, &sekizo_e3, 1, 1, 1);
    while (sekizo_e3 == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(378, 1, 0);

    while (stage_CheckAnimationFrame(378, 115, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 17);

    while (stage_CheckAnimationFrame(378, 165, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFinish(378) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;

    lt_switch_layout(54);
}

void actE3Warning(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);

    scpFadeOut(255.0f, 0, 0, 0);

    enable_game_pause = 0;

    _ACTWait(1);

    scpBoyControlReadDisable = 1;
    InitStageLight(stage_no);

    RequestStageChange(1, boyGObj, 0, 255.0f, 0.0f);
}

void actE3Inst1(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);
    const StgPre *pre;

    _ACTWait(1);

    scpBoyControlReadDisable = 1;
    if ((ICO_WORD)boyGObj != 0) {
        scpPlayMot(boyGObj, 0);
    }

    pre = &stageData[stage_no];
    stgmgrNextStagePreLoadForceStageSet(exitData[pre->ent[0]].nextStage);

    inst1_mes[0].func = actE3Inst1Chk;
    sub->mail = inst1_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actE3Door(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (gflagChk(359) == 0) {
        stage_SetAnimation(953, 0, 0);

        door_mes[0].func = actE3DoorMain;
        sub->mail = door_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(953, 0, -1);
    }
}

void actE3CageFallReady(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (gflagChk(360) == 0) {
        cageFallReady_mes[0].func = actE3CageFallReadyChk;
        sub->mail = cageFallReady_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actE3St13cIntro(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (gflagChk(363) == 0) {
        st13cIntro_mes[0].func = actE3St13cIntroChk;
        sub->mail = st13cIntro_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actE3St13cGene1(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(364) == 0) {
        _ACTWait(1);
    }
    _ACTWait(400);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_MaskOff(self);
}

void actE3St13cGene2(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(364) == 0) {
        _ACTWait(1);
    }
    Generator_Call(self);
    _ACTWait(120);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_MaskOff(self);
}

void actE3Floor(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
}

void actE3St01bEne(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (gflagChk(362) == 0) {
        st01bEne_mes[0].func = actE3St01bEneChk;
        sub->mail = st01bEne_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actE3St01bGene1(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(362) == 0) {
        _ACTWait(1);
    }
    _ACTWait(30);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_MaskOff(self);
}

void actE3St01bGene2(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(362) == 0) {
        _ACTWait(1);
    }
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_MaskOff(self);
}

void actE3St01bGene3(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(362) == 0) {
        _ACTWait(1);
    }
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_MaskOff(self);
}

void actE3St09aBrg(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (gflagChk(86) == 0) {
        stage_SetAnimation(378, 0, 0);

        st09aBrg_mes[0].func = actE3St09aBrgMain;
        sub->mail = st09aBrg_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(378, 0, -1);
    }
}

void actE3Gate(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    MallocStreamMotionBuffer();

    scpSearchGobj(3382)->active = 0;

    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    scpPlayMot(boyGObj, 0);

    scpFadeOut(255.0f, 0, 0, 0);

    stage_SetAnimation(269, 0, 0);
    stage_SetAnimation(272, 0, 0);

    gate_mes[0].func = actE3GateChk;
    sub->mail = gate_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actE3CapsuleChk(GObj *volatile self)
{
    scpSekizouCheckPoint();

    gflagOn(358);

    scpAdpcmPlayRequestFunc(2, &e3capsule, 1, 1, 1);
    while (e3capsule == 0) {
        _ACTWait(1);
    }

    scpFadeIn(6.0f);

    capsuleDemoThread = actCreateSubThread(actE3CapsuleDemo, 21);
}

inline void actE3DoorMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 0;

    sub->mainMail = doorMain_mes;

    while (1) {
        _ACTWait(1);
    }
}

inline void actE3DoorSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    doorSwitch_mes[0].func = actE3DoorUp;
    sub->mail = doorSwitch_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

inline void actE3DoorUp(GObj *volatile self)
{
    lt_switch_layout(55);
    gflagOn(359);

    _ACTWait(60);

    stage_SetAnimation(953, 1, 0);

    soundSeDefPlay(1221, 0, doorUpPos, 1);

    _ACTWait(30);

    soundSeDefPlay(1222, 0, doorUpPos, 1);

    while (stage_CheckAnimationFinish(953) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;

    lt_switch_layout(54);
}

inline void actE3St13cIntroChk(GObj *volatile self)
{
    lt_switch_layout(55);
    gflagOn(363);

    scpBoyControlReadDisable = 1;
    _ACTWait(120);

    stage_SetAnimation(954, 1, 0);

    while (stage_CheckAnimationFinish(954) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(54);

    scpBoyControlReadDisable = 0;
}

inline void actE3CageFallReadyChk(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    e3cage = 0;
    _ACTWait(1);
    while (scpTriggerFloorAttr(boyGObj, 0x2000000) == 0) {
        _ACTWait(1);
    }
    scpAdpcmPlayRequestFunc(3, &e3cage, 1, 1, 0);
}

inline void actE3St01bEneChk(GObj *volatile self)
{
    if ((ICO_WORD)girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);

    scpBoyControlReadDisable = 1;
    _ACTWait(30);

    gflagOn(362);

    stage_SetAnimation(182, 1, 0);

    while (stage_CheckAnimationFinish(182) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(54);

    scpBoyControlReadDisable = 0;
}

inline void actE3St09aBrgMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = st09aBrgMain_mes;

    while (1) {
        _ACTWait(1);
    }
}

static inline void actE3St09aBrgSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    st09aBrgSwitch_mes[0].func = actE3St09aBrgDown;
    sub->mail = st09aBrgSwitch_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}
