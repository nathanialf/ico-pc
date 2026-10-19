#include "st19a.h"
#include "layout_texture.h"
#include "thread.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "act.h"
#include "gobj_process.h"
#include "commonact.h"
#include "camera-root.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "motionManager2.h"
#include "typedef.h"
#include "main.h"
#include "script.h"

static void actSt19aChainDownSub(GObj *volatile self);

/* .sbss: the demo's own end flag, raised by the subthread the wait loop below
   spins for. */
static int demoEnd;

/* .data: actor mail packets. */

static ActMail oriMain_mes[2] = {{407, actSt19aOriSwitch}, {429}}; /* derived name */

static ActMail ori_mes[2] = {{430}, {429}}; /* derived name */

static ActMail oriSwitch_mes[2] = {{430}, {429}}; /* derived name */

static float oriXLPos[4] = {-642.0f, 2132.0f, -2861.0f, 0.0f}; /* derived name */

static float haguruma2Pos[4] = {486.0f, 2386.0f, -2917.0f, 0.0f}; /* derived name */

static ActMail haguruma_mes[2] = {{430}, {429}}; /* derived name */

static ActMail pipe_mes[2] = {{430}, {429}}; /* derived name */

static ActMail chainMain_mes[2] = {{406, actSt19aChainSwitch}, {429}}; /* derived name */

static ActMail chain_mes[2] = {{430}, {429}}; /* derived name */

static ActMail chainSwitch_mes[2] = {{430}, {429}}; /* derived name */

/* .sdata: the fence, horn and pipe stream handles. */
SqEntry *fence_up_19a = 0;

SqEntry *fence_down_19a = 0;

SqEntry *hgrm_19a = 0;

SqEntry *pipe19a = 0;

void actSt19aOriUp(GObj *volatile self)
{
    int skip = 0;
    int i;

    lt_switch_layout(PAL_JPN(55, 52));
    _ACTWait(60);
    scpAdpcmPlayRequestFunc(74, &fence_up_19a, 1, 1, 1);
    while (fence_up_19a == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(PAL_JPN(142, 138), 1, 0);
    gflagOn(310);
    while (stage_CheckAnimationFrame(PAL_JPN(142, 138), 89, 1) == 0) {
        if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
            skip = 1;
            scpFadeOut(16.0f, 0, 0, 0);
            scpAdpcmFadeCloseFunc(&fence_up_19a, 512);
            while (scpFadeChk() != 0) {
                _ACTWait(1);
            }
            while (lt_fade_status() != 2) {
                _ACTWait(1);
            }
            stage_SetAnimation(PAL_JPN(142, 138), 0, 89);
            scpFadeIn(3.0f);
            break;
        }
        _ACTWait(1);
    }
    for (i = 120; i-- > 0 && skip == 0;) {
        if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
            skip = 1;
        }
    }
    if (skip == 0) {
        scpAdpcmFadeCloseFunc(&fence_up_19a, 256);
    }
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPN(54, 51));
}

/* A 16-byte constant vector template: the float view carries the values,
   the long long view is the one the copy reads. */

static const ConstVec hagurumaPos = {{-642.0f, 2132.0f, -2861.0f, 0.0f}}; /* derived name */

void actSt19aHaguruma(GObj *volatile self)
{
    long long pos[2];
    Act *sub;
    GObj *x = self;

    sub = actInitialize(self);
    _ACTWait(1);
    pos[0] = hagurumaPos.d[0];
    pos[1] = hagurumaPos.d[1];
    soundSeDefPlay(1349, 0, (float *)pos, 1);
    if (gflagChk(311) == 0) {
        scpSearchGobj(1960)->active = 0;
        stage_SetAnimation(PAL_JPN(139, 135), 0, 0);
        stage_SetAnimation(PAL_JPN(140, 136), -1, -2);
        haguruma_mes[0].func = actSt19aHagurumaChk;
        sub->mail = haguruma_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        soundSeDefPlay(1350, 0, haguruma2Pos, 1);
        soundSeDefPlay(1351, 0, haguruma2Pos, 1);
        soundSeDefPlay(1352, 0, haguruma2Pos, 1);
        scpSearchGobj(1961)->active = 0;
        stage_SetAnimation(PAL_JPN(139, 135), 1, 0);
        stage_SetLoopFlag(PAL_JPN(139, 135), 1);
        stage_SetLoopFlag(PAL_JPN(140, 136), 1);
    }
}

void actSt19aHagurumaChk(GObj *volatile self)
{
    int skip = 0;
    int i;

    while (scpTriggerBall(self, scpSearchGobj(1961), 220.0f) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(PAL_JPN(55, 52));
    scpBoyControlReadDisable = 1;
    gflagOn(311);
    scpAdpcmPlayRequestFunc(76, &fence_down_19a, 1, 1, 1);
    while (fence_down_19a == 0) {
        _ACTWait(1);
    }
    for (i = 60; i-- > 0 && skip == 0;) {
        if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
            skip = 1;
        }
    }
    stage_SetAnimation(PAL_JPN(141, 137), 1, 0);
    for (i = 90; i-- > 0 && skip == 0;) {
        if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
            skip = 1;
        }
    }
    _ACTWait(1);
    stage_SetAnimation(PAL_JPN(139, 135), 1, 0);
    stage_SetAnimation(PAL_JPN(140, 136), 1, 0);
    scpSearchGobj(1961)->active = 0;
    scpSearchGobj(1960)->active = 1;
    stage_SetLoopFlag(PAL_JPN(139, 135), 1);
    stage_SetLoopFlag(PAL_JPN(140, 136), 1);
    soundSeDefPlay(1350, 0, haguruma2Pos, 1);
    soundSeDefPlay(1351, 0, haguruma2Pos, 1);
    soundSeDefPlay(1352, 0, haguruma2Pos, 1);
    while (stage_CheckAnimationFinish(PAL_JPN(141, 137)) == 0) {
        if (((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) || skip != 0) {
            scpFadeOut(16.0f, 0, 0, 0);
            scpAdpcmFadeCloseFunc(&fence_down_19a, 512);
            while (scpFadeChk() != 0) {
                _ACTWait(1);
            }
            while (lt_fade_status() != 2) {
                _ACTWait(1);
            }
            stage_SetAnimation(PAL_JPN(141, 137), 0, -1);
            scpFadeIn(3.0f);
            scpBoyControlReadDisable = 0;
            goto done;
        }
        _ACTWait(1);
    }
    scpBoyControlReadDisable = 0;
done:
    lt_switch_layout(PAL_JPN(54, 51));
}

void actSt19aPipeChk(GObj *volatile self)
{
    int i;

    while (scpTriggerBall(self, boyGObj, 50.0f) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(PAL_JPN(55, 52));
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    gflagOn(312);
    scpSearchGobj(1958)->active = 0;
    _ACTWait(1);
    scpAdpcmPlayRequestFunc(84, &hgrm_19a, 1, 1, 1);
    while (hgrm_19a == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(PAL_JPN(143, 139), 1, 0);
    FeedbackWallWorkInfoToBrainSystem(boyGObj);
    scpPlayMot(boyGObj, 78);
    for (i = 300; i-- > 0;) {
        if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
            scpFadeOut(16.0f, 0, 0, 0);
            scpAdpcmFadeCloseFunc(&hgrm_19a, 512);
            while (scpFadeChk() != 0) {
                _ACTWait(1);
            }
            while (lt_fade_status() != 2) {
                _ACTWait(1);
            }
            stage_SetAnimation(PAL_JPN(143, 139), 0, -1);
            scpPlayPosSet(boyGObj, 336.0f, 3933.0f, -402.0f);
            _ACTWait(4);
            scpFadeIn(3.0f);
            break;
        }
        _ACTWait(1);
    }
    scpSearchGobj(1959)->active = 1;
    scpPlayEnd(boyGObj);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPN(54, 51));
}

void actSt19aChainDown(GObj *volatile self)
{
    GProc *th;

    lt_switch_layout(PAL_JPN(55, 52));
    scpAdpcmPlayRequestFunc(97, &pipe19a, 1, 1, 0);
    th = actCreateSubThread(actSt19aChainDownSub, 21);
    demoEnd = 0;
    while (demoEnd == 0) {
        if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
            break;
        }
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
        SetCameraFlag_LwsCutBack();
        scpFadeIn(3.0f);
    }
    scpSearchGobj(1963)->active = 1;
    stage_SetAnimation(PAL_JPN(144, 140), 0, 361);
    if (pipe19a != 0) {
        scpAdpcmFadeCloseFunc(&pipe19a, 256);
    }
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPN(54, 51));
}

void actSt19bIntro(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    if (gflagChk(314) == 0) {
        while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0) {
            _ACTWait(1);
        }
        lt_switch_layout(PAL_JPN(55, 52));
        scpBoyControlReadDisable = 1;
        gflagOn(314);
        stage_SetAnimation(PAL_JPN(137, 133), 1, 0);
        while (stage_CheckAnimationFinish(PAL_JPN(137, 133)) == 0) {
            if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
                scpFadeOut(16.0f, 0, 0, 0);
                while (scpFadeChk() != 0) {
                    _ACTWait(1);
                }
                while (lt_fade_status() != 2) {
                    _ACTWait(1);
                }
                stage_SetAnimation(PAL_JPN(137, 133), 0, -1);
                scpFadeIn(3.0f);
                break;
            }
            _ACTWait(1);
        }
        lt_switch_layout(PAL_JPN(54, 51));
        scpBoyControlReadDisable = 0;
    }
}

void actSt19aOri(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    ScpCallCameraSetTarget(4298.0f, -2342.0f, 1331.0f);

    if (gflagChk(310) == 0) {
        stage_SetAnimation(PAL_JPN(142, 138), 0, 0);

        ori_mes[0].func = actSt19aOriMain;
        act->mail = ori_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPN(142, 138), 0, 89);
    }
}

void actSt19aOriXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    soundSeDefPlay(1349, 0, oriXLPos, 1);

    stage_SetAnimation(PAL_JPN(142, 138), 0, 0);
}

void actSt19aPipe(GObj *volatile self)
{
    GObj *x = self;

    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(312) == 0) {
        scpSearchGobj(1959)->active = 0;
        stage_SetAnimation(PAL_JPN(143, 139), 0, 0);

        pipe_mes[0].func = actSt19aPipeChk;
        act->mail = pipe_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);

    } else {
        scpSearchGobj(1958)->active = 0;
        stage_SetAnimation(PAL_JPN(143, 139), 0, -1);
    }
}

void actSt19aPipeXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(312) == 0) {
        stage_SetAnimation(PAL_JPN(143, 139), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPN(143, 139), 0, -1);
    }
}

void actSt19aChain(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(313) == 0) {
        scpSearchGobj(1963)->active = 0;

        stage_SetAnimation(PAL_JPN(144, 140), 0, 0);

        chain_mes[0].func = actSt19aChainMain;
        act->mail = chain_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt19aOriMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 0;

    sub->mainMail = oriMain_mes;

    while (1) {
        _ACTWait(1);
    }
}

void actSt19aOriSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;
    oriSwitch_mes[0].func = actSt19aOriUp;
    sub->mail = oriSwitch_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt19aChainMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 0;

    sub->mainMail = chainMain_mes;

    while (1) {
        _ACTWait(1);
    }
}

void actSt19aChainSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    if (gflagChk(313) == 0) {
        chainSwitch_mes[0].func = actSt19aChainDown;
        sub->mail = chainSwitch_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static void actSt19aChainDownSub(GObj *volatile self)
{
    _ACTWait(60);

    while (pipe19a == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(pipe19a->stream);

    stage_SetAnimation(PAL_JPN(144, 140), 1, 0);

    gflagOn(313);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFrame(PAL_JPN(144, 140), 240, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(60);
}
