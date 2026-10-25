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

static ActMail oriMain_mes_pal[2] = {{407, actSt19aOriSwitch}, {429}}; /* derived name */

static ActMail oriMain_mes_ntsc[2] = {{377, actSt19aOriSwitch}, {396}}; /* derived name */

static ActMail oriMain_mes_aug6[2] = {{373, actSt19aOriSwitch}, {392}}; /* derived name */

#define oriMain_mes PAL_NTSC_AUG6(oriMain_mes_pal, oriMain_mes_ntsc, oriMain_mes_aug6)

static ActMail ori_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail ori_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail ori_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define ori_mes PAL_NTSC_AUG6(ori_mes_pal, ori_mes_ntsc, ori_mes_aug6)

static ActMail oriSwitch_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail oriSwitch_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail oriSwitch_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define oriSwitch_mes PAL_NTSC_AUG6(oriSwitch_mes_pal, oriSwitch_mes_ntsc, oriSwitch_mes_aug6)

static ActMail oriSwitch2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail oriSwitch2_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define oriSwitch2_mes PAL_AUG6(oriSwitch2_mes_ntsc, oriSwitch2_mes_aug6)

static ActMail oriDown_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail oriDown_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define oriDown_mes PAL_AUG6(oriDown_mes_ntsc, oriDown_mes_aug6)

static float oriXLPos[4] = {-642.0f, 2132.0f, -2861.0f, 0.0f}; /* derived name */

static float haguruma2Pos[4] = {486.0f, 2386.0f, -2917.0f, 0.0f}; /* derived name */

static ActMail haguruma_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail haguruma_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail haguruma_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define haguruma_mes PAL_NTSC_AUG6(haguruma_mes_pal, haguruma_mes_ntsc, haguruma_mes_aug6)

static ActMail pipe_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail pipe_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail pipe_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define pipe_mes PAL_NTSC_AUG6(pipe_mes_pal, pipe_mes_ntsc, pipe_mes_aug6)

static ActMail chainMain_mes_pal[2] = {{406, actSt19aChainSwitch}, {429}}; /* derived name */

static ActMail chainMain_mes_ntsc[2] = {{376, actSt19aChainSwitch}, {396}}; /* derived name */

static ActMail chainMain_mes_aug6[2] = {{372, actSt19aChainSwitch}, {392}}; /* derived name */

#define chainMain_mes PAL_NTSC_AUG6(chainMain_mes_pal, chainMain_mes_ntsc, chainMain_mes_aug6)

static ActMail chain_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail chain_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail chain_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define chain_mes PAL_NTSC_AUG6(chain_mes_pal, chain_mes_ntsc, chain_mes_aug6)

static ActMail chainSwitch_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail chainSwitch_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail chainSwitch_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define chainSwitch_mes                                                                            \
    PAL_NTSC_AUG6(chainSwitch_mes_pal, chainSwitch_mes_ntsc, chainSwitch_mes_aug6)

static ActMail chainSwitch2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail chainSwitch2_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define chainSwitch2_mes PAL_AUG6(chainSwitch2_mes_ntsc, chainSwitch2_mes_aug6)

static ActMail chainUp_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail chainUp_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define chainUp_mes PAL_AUG6(chainUp_mes_ntsc, chainUp_mes_aug6)

static ActMail intro_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail intro_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define intro_mes PAL_AUG6(intro_mes_ntsc, intro_mes_aug6)

/* .sdata: the fence, horn and pipe stream handles. */
SqEntry *fence_up_19a = 0;

SqEntry *fence_down_19a = 0;

SqEntry *hgrm_19a = 0;

SqEntry *pipe19a = 0;

SqEntry *chain19a = 0;

static void actSt19aOriUp_ntsc(GObj *volatile self)
{
    lt_switch_layout(51);
    _ACTWait(60);
    scpAdpcmPlayRequestFunc(72, &fence_up_19a, 1, 1, 1);
    while (fence_up_19a == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(122, 1, 0);
    gflagOn(283);
    while (stage_CheckAnimationFrame(122, 89, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    _ACTWait(120);
    if (fence_up_19a != 0) {
        scpAdpcmFadeClose(fence_up_19a, 256);
    }
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static void actSt19aOriUp_aug6(GObj *volatile self)
{
    lt_switch_layout(51);
    _ACTWait(60);
    scpAdpcmPlayRequestFunc(72, &fence_up_19a, 1, 1, 1);
    while (fence_up_19a == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(121, 1, 0);
    gflagOn(282);
    while (stage_CheckAnimationFrame(121, 89, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    _ACTWait(120);
    if (fence_up_19a != 0) {
        scpAdpcmFadeClose(fence_up_19a, 256);
    }
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actSt19aOriUp(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt19aOriUp_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt19aOriUp_aug6(self);
        return;
    }
    int skip = 0;
    int i;

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    _ACTWait(60);
    scpAdpcmPlayRequestFunc(74, &fence_up_19a, 1, 1, 1);
    while (fence_up_19a == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(PAL_JPNKORCN(142, 138), 1, 0);
    gflagOn(310);
    while (stage_CheckAnimationFrame(PAL_JPNKORCN(142, 138), 89, 1) == 0) {
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
            stage_SetAnimation(PAL_JPNKORCN(142, 138), 0, 89);
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
    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

/* A 16-byte constant vector template: the float view carries the values,
   the long long view is the one the copy reads. */

static const ConstVec hagurumaPos = {{-642.0f, 2132.0f, -2861.0f, 0.0f}}; /* derived name */

static const ConstVec hagurumaChkPos = {{486.0f, 2386.0f, -2917.0f, 0.0f}}; /* derived name */

void actSt19aOriDown(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    lt_switch_layout(51);
    _ACTWait(60);
    scpAdpcmPlayRequestFunc(73, &fence_down_19a, 1, 1, 1);
    while (fence_down_19a == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(PAL_AUG6(122, 121), 1, 90);
    gflagOff(PAL_AUG6(283, 282));
    while (stage_CheckAnimationFrame(PAL_AUG6(122, 121), 180, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    _ACTWait(120);
    if (fence_down_19a != 0) {
        scpAdpcmFadeClose(fence_down_19a, 256);
    }
    lt_switch_layout(50);
    _ACTWait(60);

    oriDown_mes[0].func = actSt19aOriMain;
    sub->mail = oriDown_mes;
    ACTSendMailCorrect(self, PAL_AUG6(397, 393));
    _ACTWait(0);
}

static void actSt19aHaguruma_ntsc(GObj *volatile self)
{
    long long pos[2];
    long long pos2[2];
    Act *sub;
    GObj *x = self;

    sub = actInitialize(self);
    _ACTWait(1);
    pos[0] = hagurumaPos.d[0];
    pos[1] = hagurumaPos.d[1];
    soundSeDefPlay(1322, 0, (float *)pos, 1);
    if (gflagChk(284) == 0) {
        scpSearchGobj(1793)->active = 0;
        stage_SetAnimation(119, 0, 0);
        haguruma_mes[0].func = actSt19aHagurumaChk;
        sub->mail = haguruma_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        pos2[0] = hagurumaChkPos.d[0];
        pos2[1] = hagurumaChkPos.d[1];
        soundSeDefPlay(1323, 0, (float *)pos2, 1);
        soundSeDefPlay(1324, 0, (float *)pos2, 1);
        soundSeDefPlay(1325, 0, (float *)pos2, 1);
        scpSearchGobj(1794)->active = 0;
        stage_SetAnimation(119, 1, 0);
        stage_SetLoopFlag(119, 1);
        stage_SetLoopFlag(120, 1);
    }
}

static void actSt19aHaguruma_aug6(GObj *volatile self)
{
    long long pos[2];
    long long pos2[2];
    Act *sub;
    GObj *x = self;

    sub = actInitialize(self);
    _ACTWait(1);
    pos[0] = hagurumaPos.d[0];
    pos[1] = hagurumaPos.d[1];
    soundSeDefPlay(1322, 0, (float *)pos, 1);
    if (gflagChk(283) == 0) {
        scpSearchGobj(1777)->active = 0;
        stage_SetAnimation(118, 0, 0);
        haguruma_mes[0].func = actSt19aHagurumaChk;
        sub->mail = haguruma_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    } else {
        pos2[0] = hagurumaChkPos.d[0];
        pos2[1] = hagurumaChkPos.d[1];
        soundSeDefPlay(1323, 0, (float *)pos2, 1);
        soundSeDefPlay(1324, 0, (float *)pos2, 1);
        soundSeDefPlay(1325, 0, (float *)pos2, 1);
        scpSearchGobj(1778)->active = 0;
        stage_SetAnimation(118, 1, 0);
        stage_SetLoopFlag(118, 1);
        stage_SetLoopFlag(119, 1);
    }
}

void actSt19aHaguruma(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt19aHaguruma_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt19aHaguruma_aug6(self);
        return;
    }
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
        stage_SetAnimation(PAL_JPNKORCN(139, 135), 0, 0);
        stage_SetAnimation(PAL_JPNKORCN(140, 136), -1, -2);
        haguruma_mes[0].func = actSt19aHagurumaChk;
        sub->mail = haguruma_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        soundSeDefPlay(1350, 0, haguruma2Pos, 1);
        soundSeDefPlay(1351, 0, haguruma2Pos, 1);
        soundSeDefPlay(1352, 0, haguruma2Pos, 1);
        scpSearchGobj(1961)->active = 0;
        stage_SetAnimation(PAL_JPNKORCN(139, 135), 1, 0);
        stage_SetLoopFlag(PAL_JPNKORCN(139, 135), 1);
        stage_SetLoopFlag(PAL_JPNKORCN(140, 136), 1);
    }
}

static void actSt19aHagurumaChk_ntsc(GObj *volatile self)
{
    long long pos[2];

    while (scpTriggerBall(self, scpSearchGobj(1794), 220.0f) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    gflagOn(284);
    scpAdpcmPlayRequestFunc(74, &hgrm_19a, 1, 1, 1);
    while (hgrm_19a == 0) {
        _ACTWait(1);
    }
    _ACTWait(60);
    stage_SetAnimation(121, 1, 0);
    _ACTWait(90);
    stage_SetAnimation(119, 1, 0);
    stage_SetAnimation(120, 1, 0);
    scpSearchGobj(1794)->active = 0;
    scpSearchGobj(1793)->active = 1;
    stage_SetLoopFlag(119, 1);
    stage_SetLoopFlag(120, 1);
    pos[0] = hagurumaChkPos.d[0];
    pos[1] = hagurumaChkPos.d[1];
    soundSeDefPlay(1323, 0, (float *)pos, 1);
    soundSeDefPlay(1324, 0, (float *)pos, 1);
    soundSeDefPlay(1325, 0, (float *)pos, 1);
    while (stage_CheckAnimationFinish(121) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static void actSt19aHagurumaChk_aug6(GObj *volatile self)
{
    long long pos[2];

    while (scpTriggerBall(self, scpSearchGobj(1778), 220.0f) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    gflagOn(283);
    scpAdpcmPlayRequestFunc(74, &hgrm_19a, 1, 1, 1);
    while (hgrm_19a == 0) {
        _ACTWait(1);
    }
    _ACTWait(60);
    stage_SetAnimation(120, 1, 0);
    _ACTWait(90);
    stage_SetAnimation(118, 1, 0);
    stage_SetAnimation(119, 1, 0);
    scpSearchGobj(1778)->active = 0;
    scpSearchGobj(1777)->active = 1;
    stage_SetLoopFlag(118, 1);
    stage_SetLoopFlag(119, 1);
    pos[0] = hagurumaChkPos.d[0];
    pos[1] = hagurumaChkPos.d[1];
    soundSeDefPlay(1323, 0, (float *)pos, 1);
    soundSeDefPlay(1324, 0, (float *)pos, 1);
    soundSeDefPlay(1325, 0, (float *)pos, 1);
    while (stage_CheckAnimationFinish(120) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actSt19aHagurumaChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt19aHagurumaChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt19aHagurumaChk_aug6(self);
        return;
    }
    int skip = 0;
    int i;

    while (scpTriggerBall(self, scpSearchGobj(1961), 220.0f) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(PAL_JPNKORCN(55, 52));
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
    stage_SetAnimation(PAL_JPNKORCN(141, 137), 1, 0);
    for (i = 90; i-- > 0 && skip == 0;) {
        if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
            skip = 1;
        }
    }
    _ACTWait(1);
    stage_SetAnimation(PAL_JPNKORCN(139, 135), 1, 0);
    stage_SetAnimation(PAL_JPNKORCN(140, 136), 1, 0);
    scpSearchGobj(1961)->active = 0;
    scpSearchGobj(1960)->active = 1;
    stage_SetLoopFlag(PAL_JPNKORCN(139, 135), 1);
    stage_SetLoopFlag(PAL_JPNKORCN(140, 136), 1);
    soundSeDefPlay(1350, 0, haguruma2Pos, 1);
    soundSeDefPlay(1351, 0, haguruma2Pos, 1);
    soundSeDefPlay(1352, 0, haguruma2Pos, 1);
    while (stage_CheckAnimationFinish(PAL_JPNKORCN(141, 137)) == 0) {
        if (((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) || skip != 0) {
            scpFadeOut(16.0f, 0, 0, 0);
            scpAdpcmFadeCloseFunc(&fence_down_19a, 512);
            while (scpFadeChk() != 0) {
                _ACTWait(1);
            }
            while (lt_fade_status() != 2) {
                _ACTWait(1);
            }
            stage_SetAnimation(PAL_JPNKORCN(141, 137), 0, -1);
            scpFadeIn(3.0f);
            scpBoyControlReadDisable = 0;
            goto done;
        }
        _ACTWait(1);
    }
    scpBoyControlReadDisable = 0;
done:
    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

static void actSt19aPipeChk_ntsc(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 50.0f) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    gflagOn(285);
    scpSearchGobj(1791)->active = 0;
    _ACTWait(1);
    scpAdpcmPlayRequestFunc(82, &pipe19a, 1, 1, 1);
    while (pipe19a == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(123, 1, 0);
    FeedbackWallWorkInfoToBrainSystem(boyGObj);
    scpPlayMot(boyGObj, 78);
    _ACTWait(300);
    scpSearchGobj(1792)->active = 1;
    scpPlayEnd(boyGObj);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static void actSt19aPipeChk_aug6(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 50.0f) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    gflagOn(284);
    scpSearchGobj(1775)->active = 0;
    _ACTWait(1);
    scpAdpcmPlayRequestFunc(82, &pipe19a, 1, 1, 1);
    while (pipe19a == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(122, 1, 0);
    FeedbackWallWorkInfoToBrainSystem(boyGObj);
    scpPlayMot(boyGObj, 78);
    _ACTWait(300);
    scpSearchGobj(1776)->active = 1;
    scpPlayEnd(boyGObj);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actSt19aPipeChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt19aPipeChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt19aPipeChk_aug6(self);
        return;
    }
    int i;

    while (scpTriggerBall(self, boyGObj, 50.0f) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    gflagOn(312);
    scpSearchGobj(1958)->active = 0;
    _ACTWait(1);
    scpAdpcmPlayRequestFunc(84, &hgrm_19a, 1, 1, 1);
    while (hgrm_19a == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(PAL_JPNKORCN(143, 139), 1, 0);
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
            stage_SetAnimation(PAL_JPNKORCN(143, 139), 0, -1);
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
    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

static void actSt19aChainDown_ntsc(GObj *volatile self)
{
    lt_switch_layout(51);
    _ACTWait(60);
    scpAdpcmPlayRequestFunc(95, &chain19a, 1, 1, 1);
    while (chain19a == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(124, 1, 0);
    gflagOn(286);
    SetCameraFlag_LwsCutBack();
    while (stage_CheckAnimationFrame(124, 240, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpSearchGobj(1796)->active = 1;
    stage_SetAnimation(124, 0, 361);
    if (chain19a != 0) {
        scpAdpcmFadeClose(chain19a, 256);
    }
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static void actSt19aChainDown_aug6(GObj *volatile self)
{
    lt_switch_layout(51);
    _ACTWait(60);
    scpAdpcmPlayRequestFunc(95, &chain19a, 1, 1, 1);
    while (chain19a == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(123, 1, 0);
    gflagOn(285);
    SetCameraFlag_LwsCutBack();
    while (stage_CheckAnimationFrame(123, 240, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpSearchGobj(1780)->active = 1;
    stage_SetAnimation(123, 0, 361);
    if (chain19a != 0) {
        scpAdpcmFadeClose(chain19a, 256);
    }
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actSt19aChainDown(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt19aChainDown_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt19aChainDown_aug6(self);
        return;
    }
    GProc *th;

    lt_switch_layout(PAL_JPNKORCN(55, 52));
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
    stage_SetAnimation(PAL_JPNKORCN(144, 140), 0, 361);
    if (pipe19a != 0) {
        scpAdpcmFadeCloseFunc(&pipe19a, 256);
    }
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

void actSt19aChainUp(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    lt_switch_layout(51);
    _ACTWait(60);
    scpSearchGobj(PAL_AUG6(1796, 1780))->active = 0;
    stage_SetAnimation(PAL_AUG6(124, 123), 1, 240);
    gflagOff(PAL_AUG6(286, 285));
    SetCameraFlag_LwsCutBack();
    while (stage_CheckAnimationFrame(PAL_AUG6(124, 123), 359, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    if (chain19a != 0) {
        scpAdpcmFadeClose(chain19a, 256);
    }
    _ACTWait(60);
    lt_switch_layout(50);

    chainUp_mes[0].func = actSt19aChainMain;
    sub->mail = chainUp_mes;
    ACTSendMailCorrect(self, PAL_AUG6(397, 393));
    _ACTWait(0);
}

static void actSt19bIntro_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(287) == 0) {
        intro_mes[0].func = actSt19bIntroChk;
        act->mail = intro_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actSt19bIntro_aug6(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(286) == 0) {
        intro_mes[0].func = actSt19bIntroChk;
        act->mail = intro_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt19bIntro(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt19bIntro_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt19bIntro_aug6(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    if (gflagChk(314) == 0) {
        while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0) {
            _ACTWait(1);
        }
        lt_switch_layout(PAL_JPNKORCN(55, 52));
        scpBoyControlReadDisable = 1;
        gflagOn(314);
        stage_SetAnimation(PAL_JPNKORCN(137, 133), 1, 0);
        while (stage_CheckAnimationFinish(PAL_JPNKORCN(137, 133)) == 0) {
            if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
                scpFadeOut(16.0f, 0, 0, 0);
                while (scpFadeChk() != 0) {
                    _ACTWait(1);
                }
                while (lt_fade_status() != 2) {
                    _ACTWait(1);
                }
                stage_SetAnimation(PAL_JPNKORCN(137, 133), 0, -1);
                scpFadeIn(3.0f);
                break;
            }
            _ACTWait(1);
        }
        lt_switch_layout(PAL_JPNKORCN(54, 51));
        scpBoyControlReadDisable = 0;
    }
}

static void actSt19aOri_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    ScpCallCameraSetTarget(4298.0f, -2342.0f, 1331.0f);

    if (gflagChk(283) != 0) {
        stage_SetAnimation(122, 0, 89);
    } else {
        stage_SetAnimation(122, 0, 0);
    }

    ori_mes[0].func = actSt19aOriMain;
    act->mail = ori_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static void actSt19aOri_aug6(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    ScpCallCameraSetTarget(4298.0f, -2342.0f, 1331.0f);

    if (gflagChk(282) != 0) {
        stage_SetAnimation(121, 0, 89);
    } else {
        stage_SetAnimation(121, 0, 0);
    }

    ori_mes[0].func = actSt19aOriMain;
    act->mail = ori_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt19aOri(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt19aOri_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt19aOri_aug6(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    ScpCallCameraSetTarget(4298.0f, -2342.0f, 1331.0f);

    if (gflagChk(310) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(142, 138), 0, 0);

        ori_mes[0].func = actSt19aOriMain;
        act->mail = ori_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(142, 138), 0, 89);
    }
}

void actSt19aOriXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    /* the 2001 builds pass a copy of hagurumaPos, the same point */
    soundSeDefPlay(PAL_NTSCAUG6(1349, 1322), 0, oriXLPos, 1);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(142, 138, 122, 121), 0, 0);
}

void actSt19aPipe(GObj *volatile self)
{
    GObj *x = self;

    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSC_AUG6(312, 285, 284)) == 0) {
        scpSearchGobj(PAL_NTSC_AUG6(1959, 1792, 1776))->active = 0;
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(143, 139, 123, 122), 0, 0);

        pipe_mes[0].func = actSt19aPipeChk;
        act->mail = pipe_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);

    } else {
        scpSearchGobj(PAL_NTSC_AUG6(1958, 1791, 1775))->active = 0;
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(143, 139, 123, 122), 0, -1);
    }
}

void actSt19aPipeXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSC_AUG6(312, 285, 284)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(143, 139, 123, 122), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(143, 139, 123, 122), 0, -1);
    }
}

static void actSt19aChain_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(286) == 0) {
        scpSearchGobj(1796)->active = 0;

        stage_SetAnimation(124, 0, 0);
    }

    chain_mes[0].func = actSt19aChainMain;
    act->mail = chain_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static void actSt19aChain_aug6(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(285) == 0) {
        scpSearchGobj(1780)->active = 0;

        stage_SetAnimation(123, 0, 0);
    }

    chain_mes[0].func = actSt19aChainMain;
    act->mail = chain_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt19aChain(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt19aChain_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt19aChain_aug6(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(313) == 0) {
        scpSearchGobj(1963)->active = 0;

        stage_SetAnimation(PAL_JPNKORCN(144, 140), 0, 0);

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

    if (ICO_NTSC) {
        if (gflagChk(283) != 0) {
            oriSwitch_mes[0].func = actSt19aOriDown;
            sub->mail = oriSwitch_mes;
            ACTSendMailCorrect(self, 397);
            _ACTWait(0);
        }

        oriSwitch2_mes[0].func = actSt19aOriUp;
        sub->mail = oriSwitch2_mes;
        ACTSendMailCorrect(self, 397);
    } else if (ICO_AUG6) {
        if (gflagChk(282) != 0) {
            oriSwitch_mes[0].func = actSt19aOriDown;
            sub->mail = oriSwitch_mes;
            ACTSendMailCorrect(self, 393);
            _ACTWait(0);
        }

        oriSwitch2_mes[0].func = actSt19aOriUp;
        sub->mail = oriSwitch2_mes;
        ACTSendMailCorrect(self, 393);
    } else {
        oriSwitch_mes[0].func = actSt19aOriUp;
        sub->mail = oriSwitch_mes;
        ACTSendMailCorrect(self, 430);
    }
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

static void actSt19aChainSwitch_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    if (gflagChk(286) == 0) {
        chainSwitch_mes[0].func = actSt19aChainDown;
        sub->mail = chainSwitch_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        chainSwitch2_mes[0].func = actSt19aChainUp;
        sub->mail = chainSwitch2_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actSt19aChainSwitch_aug6(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    if (gflagChk(285) == 0) {
        chainSwitch_mes[0].func = actSt19aChainDown;
        sub->mail = chainSwitch_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    } else {
        chainSwitch2_mes[0].func = actSt19aChainUp;
        sub->mail = chainSwitch2_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt19aChainSwitch(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt19aChainSwitch_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt19aChainSwitch_aug6(self);
        return;
    }
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

void actSt19bIntroChk(GObj *volatile self)
{
    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    gflagOn(PAL_AUG6(287, 286));
    stage_SetAnimation(PAL_AUG6(117, 116), 1, 0);
    while (stage_CheckAnimationFinish(PAL_AUG6(117, 116)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
}

static void actSt19aChainDownSub(GObj *volatile self)
{
    _ACTWait(60);

    while (pipe19a == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(pipe19a->stream);

    stage_SetAnimation(PAL_JPNKORCN(144, 140), 1, 0);

    gflagOn(313);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(144, 140), 240, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(60);
}
