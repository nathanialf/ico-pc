#include "st07a.h"
#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "act.h"
#include "gobj_process.h"
#include "commonact.h"
#include "way_llf.h"
#include "brain.h"
#include "camera-root.h"
#include "generator.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "script.h"
#include "RegistPacket.h"
#include "StageAnimation.h"
#include "attackCheckBoundary.h"
#include "candle.h"
#include "geometryManager.h"
#include "item.h"
#include "staticBlur.h"
#include <libvu0.h>
#include "typedef.h"
#include "e3.h"
#include "main.h"

static void actSt07aChanChainChk(GObj *volatile self);
static void actSt07aChanReadyChk(GObj *volatile self);

static void actSt07aInit_ntsc(void)
{
    if (gflagChk(113) != 0) {
        if (gflagChk(114) != 0) {
            SetWayGroupActive(11, 0);
            SetWayGroupActive(12, 0);
            stage_SetAnimation(323, 0, -1);
        } else {
            SetWayGroupActive(13, 0);
            SetWayGroupActive(14, 0);
            SetWayGroupActive(15, 0);
            stage_SetAnimation(322, 0, -1);
        }
    } else {
        SetWayGroupActive(13, 0);
        SetWayGroupActive(14, 0);
        SetWayGroupActive(15, 0);

        stage_SetAnimation(322, 0, 0);
    }
}

static void actSt07aInit_aug6jun26(void)
{
    if (gflagChk(113) != 0) {
        if (gflagChk(114) != 0) {
            SetWayGroupActive(11, 0);
            SetWayGroupActive(12, 0);
            stage_SetAnimation(322, 0, -1);
        } else {
            SetWayGroupActive(13, 0);
            SetWayGroupActive(14, 0);
            SetWayGroupActive(15, 0);
            stage_SetAnimation(321, 0, -1);
        }
    } else {
        SetWayGroupActive(13, 0);
        SetWayGroupActive(14, 0);
        SetWayGroupActive(15, 0);

        stage_SetAnimation(321, 0, 0);
    }
}

void actSt07aInit(void)
{
    if (ICO_NTSC) {
        actSt07aInit_ntsc();
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt07aInit_aug6jun26();
        return;
    }
    if (gflagChk(126) != 0) {
        ((GObj *)scpSearchGobj(397))->active = 0;

        stage_SetAnimation(PAL_JPNKORCN(360, 356), -1, -2);

        FinishHint(3);

        if (gflagChk(128) != 0) {
            SetWayGroupActive(11, 0);
            SetWayGroupActive(12, 0);
            stage_SetAnimation(PAL_JPNKORCN(357, 353), 0, -1);
        } else {
            SetWayGroupActive(13, 0);
            SetWayGroupActive(14, 0);
            SetWayGroupActive(15, 0);
            stage_SetAnimation(PAL_JPNKORCN(356, 352), 0, -1);
        }
    } else {
        scpLinkBGAtoLayoutedTarget(397, PAL_JPNKORCN(360, 356));

        SetWayGroupActive(13, 0);
        SetWayGroupActive(14, 0);
        SetWayGroupActive(15, 0);

        stage_SetAnimation(PAL_JPNKORCN(356, 352), 0, 0);
        stage_SetAnimation(PAL_JPNKORCN(355, 351), 0, 0);
    }
}

void actSt07aEnd(void)
{
    if (girlGObj != 0 && gflagChk(PAL_NTSCAUG6JUN26(131, 117)) == 0 &&
        gflagChk(PAL_NTSCAUG6JUN26(128, 114)) != 0) {
        gflagOn(PAL_NTSC_AUG6JUN26(391, 358, 357));
    }
    if (girlGObj != 0 && gflagChk(PAL_NTSCAUG6JUN26(134, 120)) == 0 &&
        gflagChk(PAL_NTSCAUG6JUN26(174, 155)) != 0) {
        gflagOn(PAL_NTSC_AUG6JUN26(391, 358, 357));
    }
}

/* .sbss: the demo's own end flag, raised by the subthreads the wait loops
   below spin for. */
static int demoEnd;

/* A 16-byte constant vector template: the float view carries the values,
   the long long view is the one the copy reads. */

static const ConstVec chanChkPos = {{10000.0f, 0.0f, 0.0f, 1.0f}}; /* derived name */

static const ConstVec chanWay1Pos = {{-875.0f, 77.0f, -958.0f, 0.0f}}; /* derived name */

static const ConstVec chanWay2Pos = {{-264.0f, 1277.0f, -292.0f, 0.0f}}; /* derived name */

static const ConstVec tsuroChkPos = {{-123.0f, -72.0f, -945.0f, 0.0f}}; /* derived name */

static const EffectArg tsuroEffectPos = {{-1793.0f, 132.0f, -942.0f, 1.0f}}; /* derived name */

static const EffectArg tsuroEffect2Pos = {{-2406.0f, 182.0f, -975.0f, 1.0f}}; /* derived name */

static const EffectArg tsuroEffect3Pos = {{-1005.0f, 1349.0f, -493.0f, 1.0f}}; /* derived name */

static const EffectArg tsuroEffect4Pos = {{-1320.0f, 1346.0f, -806.0f, 1.0f}}; /* derived name */

static const EffectArg tsuroEffect5Pos = {{-1885.0f, 1360.0f, -842.0f, 1.0f}}; /* derived name */

static const EffectArg tsuroEffect6Pos = {{-1864.0f, 1360.0f, -1106.0f, 1.0f}}; /* derived name */

static const EffectArg tsuroEffect7Pos = {{-1327.0f, 1327.0f, -1141.0f, 1.0f}}; /* derived name */

static const EffectArg tsuroEffect8Pos = {{-1223.0f, 1325.0f, -1101.0f, 1.0f}}; /* derived name */

static const EffectArg tsuroEffect9Pos = {{-1273.0f, 1325.0f, -873.0f, 1.0f}}; /* derived name */

/* .sdata: the bridge and statue stream handles and the statue's shake, then
   the shake's volume. */
SqEntry *bridge = 0;

SqEntry *sekizo7a = 0;

SqEntry *sekizo_7a = 0;

int sekizo_7a_vol = 0;

/* the 2001 builds keep the pad-shake handle and its volume in these two (sekizo_7a and
   sekizo_7a_vol there; the European program keeps a stream handle in sekizo_7a and the
   shake in sekizo_7a_vol) */
int sekizo_7a_ntscaug6jun26 = 0;

unsigned char sekizo_7a_vol_ntscaug6jun26 = 0;

static unsigned char st07a_yure_vol = 0; /* derived name */

static void actSt07aChanChk_ntsc(GObj *volatile self)
{
    long long buf[2];
    float dir[4];
    SqEntry *chan = 0;
    GProc *hEffect;
    GProc *hMot;
    GProc *hFall;

    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    soundSeDefPlay(1289, 0, 0, 1);

    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        actCreateSubThread(actSt07aChanWay1, 21);
    }
    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x5000000) != 0) {
        actCreateSubThread(actSt07aChanWay2, 21);
    }

    scpSleepEnemyAll();

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    scpPlayStart(boyGObj);

    gflagOn(113);

    _ACTWait(90);

    scpAdpcmPlayRequestFunc(57, &chan, 1, 1, 1);
    while (chan == 0) {
        _ACTWait(1);
    }

    hEffect = actCreateSubThread(actSt07aChanEffect, 21);

    hMot = actCreateSubThread(actSt07aChanMot, 21);

    hFall = actCreateSubThread(actSt07aChanFall, 21);

    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    if (demoEnd == 0) {
        scpAdpcmFadeClose(chan, 192);
        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }

        iosThreadSetPri(&hEffect->thread, 34);
        iosThreadSetPri(&hMot->thread, 34);
        iosThreadSetPri(&hFall->thread, 34);

        stage_SetAnimation(322, 1, -1);
        scpPlayMot(boyGObj, 465);

        DeleteLayoutedCandleParticleEffect();
        _ACTWait(1);

        scpTorchLightOn(394);
        scpTorchLightOn(395);

        scpFadeIn(3.0f);
    } else {
        iosThreadSetPri(&hEffect->thread, 34);
        iosThreadSetPri(&hMot->thread, 34);
        iosThreadSetPri(&hFall->thread, 34);
    }

    buf[0] = chanChkPos.d[0];
    buf[1] = chanChkPos.d[1];
    sceVu0SubVector(dir, buf, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpPlayMot(boyGObj, 0);

    scpPlayEnd(boyGObj);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    scpWakeupEnemyAll();
}

static void actSt07aChanChk_aug6jun26(GObj *volatile self)
{
    long long buf[2];
    float dir[4];
    SqEntry *chan = 0;
    GProc *hEffect;
    GProc *hMot;
    GProc *hFall;

    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    soundSeDefPlay(1289, 0, 0, 1);

    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        actCreateSubThread(actSt07aChanWay1, 21);
    }
    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x5000000) != 0) {
        actCreateSubThread(actSt07aChanWay2, 21);
    }

    scpSleepEnemyAll();

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    scpPlayStart(boyGObj);

    gflagOn(113);

    _ACTWait(90);

    scpAdpcmPlayRequestFunc(57, &chan, 1, 1, 1);
    while (chan == 0) {
        _ACTWait(1);
    }

    hEffect = actCreateSubThread(actSt07aChanEffect, 21);

    hMot = actCreateSubThread(actSt07aChanMot, 21);

    hFall = actCreateSubThread(actSt07aChanFall, 21);

    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    if (demoEnd == 0) {
        scpAdpcmFadeClose(chan, 192);
        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }

        iosThreadSetPri(&hEffect->thread, 34);
        iosThreadSetPri(&hMot->thread, 34);
        iosThreadSetPri(&hFall->thread, 34);

        stage_SetAnimation(321, 1, -1);
        scpPlayMot(boyGObj, 465);

        DeleteLayoutedCandleParticleEffect();
        _ACTWait(1);

        scpTorchLightOn(393);
        scpTorchLightOn(394);

        scpFadeIn(3.0f);
    } else {
        iosThreadSetPri(&hEffect->thread, 34);
        iosThreadSetPri(&hMot->thread, 34);
        iosThreadSetPri(&hFall->thread, 34);
    }

    buf[0] = chanChkPos.d[0];
    buf[1] = chanChkPos.d[1];
    sceVu0SubVector(dir, buf, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpPlayMot(boyGObj, 0);

    scpPlayEnd(boyGObj);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    scpWakeupEnemyAll();
}

void actSt07aChanChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt07aChanChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt07aChanChk_aug6jun26(self);
        return;
    }
    long long buf[2];
    float dir[4];
    GProc *hEffect;
    GProc *hMot;
    GProc *hFall;

    while (gflagChk(127) == 0) {
        _ACTWait(1);
    }

    soundSeDefPlay(1313, 0, 0, 1);

    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        actCreateSubThread(actSt07aChanWay1, 21);
    }
    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x5000000) != 0) {
        actCreateSubThread(actSt07aChanWay2, 21);
    }

    scpSleepEnemyAll();
    FinishHint(3);

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;

    stage_SetAnimation(PAL_JPNKORCN(355, 351), 1, 0);

    _ACTWait(1);

    ((GObj *)scpSearchGobj(397))->active = 0;
    stage_SetAnimation(PAL_JPNKORCN(360, 356), -1, -2);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(355, 351)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayStart(boyGObj);

    if (girlGObj != 0) {
        scpPlayStart(girlGObj);
    }

    gflagOn(126);

    while (bridge == 0) {
        _ACTWait(1);
    }
    AdpcmPlay(bridge->stream);

    hEffect = actCreateSubThread(actSt07aChanEffect, 21);

    hMot = actCreateSubThread(actSt07aChanMot, 21);

    hFall = actCreateSubThread(actSt07aChanFall, 21);

    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    if (demoEnd == 0) {
        scpAdpcmFadeCloseFunc(&bridge, 192);
        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }

        iosThreadSetPri(&hEffect->thread, 34);
        iosThreadSetPri(&hMot->thread, 34);
        iosThreadSetPri(&hFall->thread, 34);

        stage_SetAnimation(PAL_JPNKORCN(356, 352), 1, -1);
        scpPlayMot(boyGObj, 475);

        DeleteLayoutedCandleParticleEffect();
        _ACTWait(1);

        scpTorchLightOn(431);
        scpTorchLightOn(432);

        SetCameraFlag_GamecamCutBack();
        scpFadeIn(3.0f);
    } else {
        iosThreadSetPri(&hEffect->thread, 34);
        iosThreadSetPri(&hMot->thread, 34);
        iosThreadSetPri(&hFall->thread, 34);
    }

    buf[0] = chanChkPos.d[0];
    buf[1] = chanChkPos.d[1];
    sceVu0SubVector(dir, buf, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpPlayMot(boyGObj, 0);

    scpPlayEnd(boyGObj);

    if (girlGObj != 0) {
        scpPlayEnd(girlGObj);
    }

    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpWakeupEnemyAll();
}

static const EffectArg chanEffectPos = {{-1617.0f, -958.0f, -957.15f, 1.0f}}; /* derived name */

static const EffectArg chanEffect2Pos = {{-1641.0f, 80.0f, -848.15f, 1.0f}}; /* derived name */

static const EffectArg chanEffect3Pos = {{-1669.0f, 73.0f, -765.15f, 1.0f}}; /* derived name */

static const EffectArg chanEffect4Pos = {{-1689.0f, 191.0f, -624.15f, 1.0f}}; /* derived name */

static const EffectArg chanEffect5Pos = {{-1700.0f, 180.0f, -524.15f, 1.0f}}; /* derived name */

static const EffectArg chanEffect6Pos = {{-1745.0f, 106.0f, -632.15f, 1.0f}}; /* derived name */

static const EffectArg chanEffect7Pos = {{-1352.0f, 1344.0f, -561.15f, 1.0f}}; /* derived name */

static const EffectArg chanEffect8Pos = {{-1411.0f, 1327.0f, -459.15f, 1.0f}}; /* derived name */

static const EffectArg chanEffect9Pos = {{-1562.0f, 1630.0f, 451.15f, 1.0f}}; /* derived name */

static void actSt07aChanEffect_ntsc(GObj *volatile self)
{
    EffectArg b1;
    EffectArg b2;
    EffectArg b3;
    EffectArg b4;
    EffectArg b5;
    EffectArg b6;
    EffectArg b7;
    EffectArg b8;
    EffectArg b9;
    EffectArg b10;
    float t;
    float tn;
    int n;

    t = 0.0f;
    do {
        switch ((int)t) {
        case 250:
            b1 = chanEffectPos;
            scpEffectStart(&b1, 16);
            b2 = chanEffectPos;
            scpEffectStart(&b2, 11);
            break;
        case 300:
            iosPadActRequest(boyPad, 17);
            break;
        case 354:
            b3 = chanEffect2Pos;
            scpEffectStart(&b3, 14);
            b4 = chanEffect3Pos;
            scpEffectStart(&b4, 15);
            iosPadActRequest(boyPad, 15);
            ReviveAllCarryableItemsWithRandomVelocity(-5.0f, 0.0f);
            break;
        case 500:
            b5 = chanEffect4Pos;
            scpEffectStart(&b5, 16);
            b6 = chanEffect5Pos;
            scpEffectStart(&b6, 15);
            b7 = chanEffect6Pos;
            scpEffectStart(&b7, 14);
            break;
        case 598:
            b8 = chanEffect7Pos;
            scpEffectStart(&b8, 15);
            b9 = chanEffect8Pos;
            scpEffectStart(&b9, 14);
            break;
        case 628:
            b10 = chanEffect9Pos;
            scpEffectStart(&b10, 15);
            iosPadActRequest(boyPad, 16);
            break;
        /* frames with nothing to do.  Cases 1 and 410 are pinned by their
           compares; the case tree's split points (410 at the root, 300 and
           628 below it) pin at least one more empty case between 354 and
           410 and two above 628, and aug6's TRFILE puts no code on this
           function's lines 575..613, where the block sits.  The values
           leave no code, so 400, 700 and 800 are not pinned. */
        case 1:
        case 400:
        case 410:
        case 700:
        case 800:
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
    } while (t < 1600.0f);
    _ACTWait(0);
}

static void actSt07aChanEffect_aug6jun26(GObj *volatile self)
{
    EffectArg b1;
    EffectArg b2;
    EffectArg b3;
    EffectArg b4;
    EffectArg b5;
    EffectArg b6;
    EffectArg b7;
    EffectArg b8;
    EffectArg b9;
    EffectArg b10;
    float t;
    float tn;
    int n;

    t = 0.0f;
    do {
        switch ((int)t) {
        case 250:
            b1 = chanEffectPos;
            scpEffectStart(&b1, 16);
            b2 = chanEffectPos;
            scpEffectStart(&b2, 11);
            break;
        case 300:
            iosPadActRequest(boyPad, 17);
            break;
        case 354:
            b3 = chanEffect2Pos;
            scpEffectStart(&b3, 14);
            b4 = chanEffect3Pos;
            scpEffectStart(&b4, 15);
            iosPadActRequest(boyPad, 15);
            ReviveAllCarryableItemsWithRandomVelocity(-5.0f, 0.0f);
            break;
        case 500:
            b5 = chanEffect4Pos;
            scpEffectStart(&b5, 16);
            b6 = chanEffect5Pos;
            scpEffectStart(&b6, 15);
            b7 = chanEffect6Pos;
            scpEffectStart(&b7, 14);
            break;
        case 598:
            b8 = chanEffect7Pos;
            scpEffectStart(&b8, 15);
            b9 = chanEffect8Pos;
            scpEffectStart(&b9, 14);
            break;
        case 628:
            b10 = chanEffect9Pos;
            scpEffectStart(&b10, 15);
            iosPadActRequest(boyPad, 16);
            break;
        /* frames with nothing to do.  1 and 410 are the object's own
           compares; its case tree (410 at the root, 300 and 628 below it)
           holds one more value between 354 and 410 and two above 628, and
           aug6's TRFILE puts no code on the block's lines 575..613.  400,
           700 and 800 leave no code: derived values. */
        case 1:
        case 400:
        case 410:
        case 700:
        case 800:
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
    } while (t < 1600.0f);
    _ACTWait(0);
}

void actSt07aChanEffect(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt07aChanEffect_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt07aChanEffect_aug6jun26(self);
        return;
    }
    float t;
    float tn;
    int n;

    t = 0.0f;
    do {
        switch ((int)t) {
        case 154:
            iosPadActRequest(boyPad, 15);
            ReviveAllCarryableItemsWithRandomVelocity(-5.0f, 0.0f);
            break;
        case 84:
        case 210:
            iosPadActRequest(boyPad, 17);
            break;
        case 420:
            iosPadActRequest(boyPad, 15);
            break;
        case 434:
            iosPadActRequest(boyPad, 16);
            break;
        case 574:
            iosPadActRequest(boyPad, 17);
            break;
        case 0:
        case 112:
        case 300:
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
    } while (t < 1600.0f);
    _ACTWait(0);
}

static void actSt07aTsuroChk_ntsc(GObj *volatile self)
{
    long long buf[2];
    GObj *obj;
    int wk;
    GProc *hGirl;
    GProc *hEffect;
    GProc *hConte;

    hGirl = 0;

    while ((obj = scpIsBombExplode(19)) == 0 || scpTriggerBall(self, obj, 200.0f) == 0) {
        _ACTWait(1);
    }

    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        hGirl = actCreateSubThread(actSt07aGirlWay, 21);
    }

    gflagOn(114);

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;
    brainLockGirl();

    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(58, &bridge, 1, 1, 1);
    while (bridge == 0) {
        _ACTWait(1);
    }

    hEffect = actCreateSubThread(actSt07aTsuroEffect, 21);
    hConte = actCreateSubThread(actSt07aTsuroConte, 21);

    demoEnd = 0;
    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    wk = demoEnd ^ 1;

    if (wk != 0) {
        scpAdpcmFadeClose(bridge, 192);
        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    }

    iosPadActStopAll();
    iosThreadSetPri(&hConte->thread, 34);
    iosThreadSetPri(&hEffect->thread, 34);
    if (hGirl != 0) {
        iosThreadSetPri(&hGirl->thread, 34);
        _ACTWait(2);
        _SCPMoveCharactorByWay_Cancel(girlGObj);
    }

    if (wk != 0) {
        stage_SetAnimation(323, 0, -1);
        stage_SetAnimation(324, 0, -1);
        ReviveAllCarryableItemsWithRandomVelocity(-5.0f, 0.0f);
        _ACTWait(2);

        if (hGirl != 0) {
            buf[0] = tsuroChkPos.d[0];
            buf[1] = tsuroChkPos.d[1];
            SetDirectRootPosition(girlGObj, buf);
        }

        SetCameraFlag_GamecamCutBack();
        scpFadeIn(3.0f);
    }

    lt_switch_layout(50);

    scpBoyControlReadDisable = 0;
    brainUnlockGirl();

    gflagOn(119);

    scpWakeupEnemyAll();

    SetWayGroupActive(11, 0);
    SetWayGroupActive(12, 0);
    SetWayGroupActive(13, 1);
    SetWayGroupActive(14, 1);
    SetWayGroupActive(15, 1);

    scpTorchLightOff(394);
    scpTorchLightOff(395);
}

static void actSt07aTsuroChk_aug6jun26(GObj *volatile self)
{
    long long buf[2];
    GObj *obj;
    int wk;
    GProc *hGirl;
    GProc *hEffect;
    GProc *hConte;

    hGirl = 0;

    while ((obj = scpIsBombExplode(19)) == 0 || scpTriggerBall(self, obj, 200.0f) == 0) {
        _ACTWait(1);
    }

    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        hGirl = actCreateSubThread(actSt07aGirlWay, 21);
    }

    gflagOn(114);

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;
    brainLockGirl();

    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(58, &bridge, 1, 1, 1);
    while (bridge == 0) {
        _ACTWait(1);
    }

    hEffect = actCreateSubThread(actSt07aTsuroEffect, 21);
    hConte = actCreateSubThread(actSt07aTsuroConte, 21);

    demoEnd = 0;
    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    wk = demoEnd ^ 1;

    if (wk != 0) {
        scpAdpcmFadeClose(bridge, 192);
        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    }

    iosPadActStopAll();
    iosThreadSetPri(&hConte->thread, 34);
    iosThreadSetPri(&hEffect->thread, 34);
    if (hGirl != 0) {
        iosThreadSetPri(&hGirl->thread, 34);
        _ACTWait(2);
        _SCPMoveCharactorByWay_Cancel(girlGObj);
    }

    if (wk != 0) {
        stage_SetAnimation(322, 0, -1);
        stage_SetAnimation(323, 0, -1);
        ReviveAllCarryableItemsWithRandomVelocity(-5.0f, 0.0f);
        _ACTWait(2);

        if (hGirl != 0) {
            buf[0] = tsuroChkPos.d[0];
            buf[1] = tsuroChkPos.d[1];
            SetDirectRootPosition(girlGObj, buf);
        }

        SetCameraFlag_GamecamCutBack();
        scpFadeIn(3.0f);
    }

    lt_switch_layout(50);

    scpBoyControlReadDisable = 0;
    brainUnlockGirl();

    gflagOn(119);

    scpWakeupEnemyAll();

    SetWayGroupActive(11, 0);
    SetWayGroupActive(12, 0);
    SetWayGroupActive(13, 1);
    SetWayGroupActive(14, 1);
    SetWayGroupActive(15, 1);

    scpTorchLightOff(393);
    scpTorchLightOff(394);
}

void actSt07aTsuroChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt07aTsuroChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt07aTsuroChk_aug6jun26(self);
        return;
    }
    long long buf[2];
    GObj *obj;
    int wk;
    GProc *hGirl;
    GProc *hEffect;
    GProc *hConte;

    hGirl = 0;

    while ((obj = scpIsBombExplode(19)) == 0 || scpTriggerBall(self, obj, 200.0f) == 0) {
        _ACTWait(1);
    }

    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        hGirl = actCreateSubThread(actSt07aGirlWay, 21);
    }

    gflagOn(128);

    lt_switch_layout(PAL_JPNKORCN(55, 52));

    scpBoyControlReadDisable = 1;
    brainLockGirl();

    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(60, &sekizo7a, 1, 1, 1);
    while (sekizo7a == 0) {
        _ACTWait(1);
    }

    hEffect = actCreateSubThread(actSt07aTsuroEffect, 21);
    hConte = actCreateSubThread(actSt07aTsuroConte, 21);

    demoEnd = 0;
    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    wk = demoEnd ^ 1;

    if (wk != 0) {
        scpAdpcmFadeCloseFunc(&sekizo7a, 192);
        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    }

    iosPadActStopAll();
    iosThreadSetPri(&hConte->thread, 34);
    iosThreadSetPri(&hEffect->thread, 34);
    if (hGirl != 0) {
        iosThreadSetPri(&hGirl->thread, 34);
        _ACTWait(2);
        _SCPMoveCharactorByWay_Cancel(girlGObj);
    }

    if (wk != 0) {
        stage_SetAnimation(PAL_JPNKORCN(357, 353), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(358, 354), 0, -1);
        ReviveAllCarryableItemsWithRandomVelocity(-5.0f, 0.0f);
        _ACTWait(2);

        if (hGirl != 0) {
            buf[0] = tsuroChkPos.d[0];
            buf[1] = tsuroChkPos.d[1];
            SetDirectRootPosition(girlGObj, buf);
        }

        SetCameraFlag_GamecamCutBack();
        scpFadeIn(8.0f);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    }

    lt_switch_layout(PAL_JPNKORCN(54, 51));

    scpBoyControlReadDisable = 0;
    brainUnlockGirl();

    gflagOn(133);

    scpWakeupEnemyAll();

    SetWayGroupActive(11, 0);
    SetWayGroupActive(12, 0);
    SetWayGroupActive(13, 1);
    SetWayGroupActive(14, 1);
    SetWayGroupActive(15, 1);

    scpTorchLightOff(431);
    scpTorchLightOff(432);
}

void actSt07aTsuroEffect(GObj *volatile self)
{
    EffectArg b1;
    EffectArg b2;
    EffectArg b3;
    EffectArg b4;
    EffectArg b5;
    EffectArg b6;
    EffectArg b7;
    EffectArg b8;
    EffectArg b9;
    EffectArg b10;
    float t;
    float tn;
    int n;

    t = 0.0f;
    do {
        switch ((int)t) {
        case 212:
            b1 = tsuroEffectPos;
            scpEffectStart(&b1, 13);
            b2 = tsuroEffectPos;
            scpEffectStart(&b2, 15);
            break;
        case 438:
            b3 = tsuroEffect2Pos;
            scpEffectStart(&b3, 13);
            break;
        case 465:
            iosPadActRequest(boyPad, 16);
            break;
        case 100:
        case 180:
        case 480:
            iosPadActRequest(boyPad, 17);
            break;
        case 505:
            ReviveAllCarryableItemsWithRandomVelocity(-5.0f, 0.0f);
            b4 = tsuroEffect3Pos;
            scpEffectStart(&b4, 15);
            iosPadActRequest(boyPad, 15);
            break;
        case 510:
            b5 = tsuroEffect4Pos;
            scpEffectStart(&b5, 11);
            b6 = tsuroEffect5Pos;
            scpEffectStart(&b6, 15);
            b7 = tsuroEffect6Pos;
            scpEffectStart(&b7, 15);
            b8 = tsuroEffect7Pos;
            scpEffectStart(&b8, 11);
            break;
        case 560:
            b9 = tsuroEffect8Pos;
            scpEffectStart(&b9, 15);
            b10 = tsuroEffect9Pos;
            scpEffectStart(&b10, 15);
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
    } while (t < 600.0f);
    _ACTWait(0);
}

static void actSt07aSekizoChk_ntsc(GObj *volatile self)
{
    /* the SE handle, which the sound subsystem owns (st13c's
       actSt13cSekizoChk keeps one the same way) */
    volatile int se;
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(18, &sekizo7a, 1, 1, 1);
    while (sekizo7a == 0) {
        _ACTWait(1);
    }

    _ACTWait(60);

    scpKillEnemyOne(363);
    scpKillEnemyOne(364);
    scpKillEnemyOne(365);
    scpKillEnemyOne(366);
    scpKillEnemyOne(367);
    scpKillEnemyOne(368);
    scpKillEnemyOne(3453);
    scpKillSpiderGroup(369);

    scpMaskGeneratorAll();

    stage_SetAnimation(327, 1, 0);

    ReviveAllCarryableItemsWithNonSleepFrame(250);

    sekizo_7a_ntscaug6jun26 = iosPadActRequest(boyPad, 9);
    sekizo_7a_vol_ntscaug6jun26 = 128;
    iosPadActVolumeSet(sekizo_7a_ntscaug6jun26, 128);

    se = soundSeDefPlay(1193, 0, 0, 1);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);

    scpPlayPosSet(boyGObj, -3300.0f, 977.0f, -1050.0f);
    scpPlayPosSet(girlGObj, -3300.0f, 977.0f, -950.0f);

    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpBoyControlReadDisable = 1;

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpSekizouCheckPoint();

    scpPlayMot(girlGObj, 572);
    scpPlayWaitMotEnd(girlGObj);

    gflagOn(116);

    soundSeDefStop(se);

    while (stage_CheckAnimationFrame(327, 180, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(sekizo_7a_ntscaug6jun26);

    while (stage_CheckAnimationFinish(327) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(girlGObj, 514);
    scpPlayEnd(girlGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static void actSt07aSekizoChk_aug6jun26(GObj *volatile self)
{
    /* the SE handle, which the sound subsystem owns (st13c's
       actSt13cSekizoChk keeps one the same way) */
    volatile int se;
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    scpAdpcmPlayRequestFunc(18, &sekizo7a, 1, 1, 1);
    while (sekizo7a == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    _ACTWait(60);

    scpKillEnemyOne(362);
    scpKillEnemyOne(363);
    scpKillEnemyOne(364);
    scpKillEnemyOne(365);
    scpKillEnemyOne(366);
    scpKillEnemyOne(367);
    scpKillEnemyOne(3415);
    scpKillSpiderGroup(368);

    scpMaskGeneratorAll();

    stage_SetAnimation(326, 1, 0);

    ReviveAllCarryableItemsWithNonSleepFrame(250);

    sekizo_7a_ntscaug6jun26 = iosPadActRequest(boyPad, 9);
    sekizo_7a_vol_ntscaug6jun26 = 128;
    iosPadActVolumeSet(sekizo_7a_ntscaug6jun26, 128);

    se = soundSeDefPlay(1193, 0, 0, 1);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);

    scpPlayPosSet(boyGObj, -3300.0f, 977.0f, -1050.0f);
    scpPlayPosSet(girlGObj, -3300.0f, 977.0f, -950.0f);

    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpBoyControlReadDisable = 1;

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpSekizouCheckPoint();

    scpPlayMot(girlGObj, 573);
    scpPlayWaitMotEnd(girlGObj);

    gflagOn(116);

    soundSeDefStop(se);

    while (stage_CheckAnimationFrame(326, 180, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(sekizo_7a_ntscaug6jun26);

    while (stage_CheckAnimationFinish(326) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(girlGObj, 514);
    scpPlayEnd(girlGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
}

void actSt07aSekizoChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt07aSekizoChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt07aSekizoChk_aug6jun26(self);
        return;
    }
    /* the SE handle, which the sound subsystem owns (st13c's
       actSt13cSekizoChk keeps one the same way) */
    volatile int se;
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;

    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(19, &sekizo_7a, 1, 1, 1);
    while (sekizo_7a == 0) {
        _ACTWait(1);
    }

    scpKillEnemyOne(399);
    scpKillEnemyOne(400);
    scpKillEnemyOne(401);
    scpKillEnemyOne(402);
    scpKillEnemyOne(403);
    scpKillEnemyOne(404);
    scpKillEnemyOne(PAL_JPNKORCN(3757, 3734));
    scpKillSpiderGroup(405);

    scpMaskGeneratorAll();

    stage_SetAnimation(PAL_JPNKORCN(362, 358), 1, 0);

    ReviveAllCarryableItemsWithNonSleepFrame(250);

    sekizo_7a_vol = iosPadActRequest(boyPad, 9);
    st07a_yure_vol = 128;
    iosPadActVolumeSet(sekizo_7a_vol, 128);

    se = soundSeDefPlay(1217, 0, 0, 1);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 532);

    scpPlayPosSet(boyGObj, -3300.0f, 977.0f, -1050.0f);
    scpPlayPosSet(girlGObj, -3300.0f, 977.0f, -950.0f);

    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpBoyControlReadDisable = 1;

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpSekizouCheckPoint();

    scpPlayMot(girlGObj, 645);
    scpPlayWaitMotEnd(girlGObj);

    gflagOn(130);

    soundSeDefStop(se);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(362, 358), 180, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(sekizo_7a_vol);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(362, 358)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(girlGObj, 532);
    scpPlayEnd(girlGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

static ActMail chanReady_mes[2] = {{430}, {429}}; /* derived name */

static ActMail chanChain_mes[2] = {{430}, {429}}; /* derived name */

static ActMail chan_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail chan_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail chan_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define chan_mes PAL_NTSC_AUG6JUN26(chan_mes_pal, chan_mes_ntsc, chan_mes_aug6jun26)

static ActMail tsuro_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail tsuro_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail tsuro_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define tsuro_mes PAL_NTSC_AUG6JUN26(tsuro_mes_pal, tsuro_mes_ntsc, tsuro_mes_aug6jun26)

static ActMail intro_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail intro_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail intro_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define intro_mes PAL_NTSC_AUG6JUN26(intro_mes_pal, intro_mes_ntsc, intro_mes_aug6jun26)

static ActMail sekizo_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail sekizo_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail sekizo_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define sekizo_mes PAL_NTSC_AUG6JUN26(sekizo_mes_pal, sekizo_mes_ntsc, sekizo_mes_aug6jun26)

static ActMail ene_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail ene_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail ene_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define ene_mes PAL_NTSC_AUG6JUN26(ene_mes_pal, ene_mes_ntsc, ene_mes_aug6jun26)

static ActMail ene2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail ene2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail ene2_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define ene2_mes PAL_NTSC_AUG6JUN26(ene2_mes_pal, ene2_mes_ntsc, ene2_mes_aug6jun26)

static void actSt07aEne_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    ((GObj *)scpSearchGobj(361))->active = 0;

    ((GObj *)scpSearchGobj(372))->drawMask = 0;
    ((GObj *)scpSearchGobj(373))->drawMask = 0;

    if (gflagChk(117) == 0) {
        ((GObj *)scpSearchGobj(374))->active = 0;
        ((GObj *)scpSearchGobj(375))->active = 0;
        ((GObj *)scpSearchGobj(376))->active = 0;
        ((GObj *)scpSearchGobj(377))->active = 0;
        ((GObj *)scpSearchGobj(378))->active = 0;
        ((GObj *)scpSearchGobj(379))->active = 0;

        ene_mes[0].func = actSt07aEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actSt07aEne_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    ((GObj *)scpSearchGobj(360))->active = 0;

    if (gflagChk(117) == 0) {
        ((GObj *)scpSearchGobj(373))->active = 0;
        ((GObj *)scpSearchGobj(374))->active = 0;
        ((GObj *)scpSearchGobj(375))->active = 0;
        ((GObj *)scpSearchGobj(376))->active = 0;
        ((GObj *)scpSearchGobj(377))->active = 0;
        ((GObj *)scpSearchGobj(378))->active = 0;

        ene_mes[0].func = actSt07aEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt07aEne(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt07aEne_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt07aEne_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    ((GObj *)scpSearchGobj(396))->active = 0;

    ((GObj *)scpSearchGobj(408))->drawMask = 0;
    ((GObj *)scpSearchGobj(409))->drawMask = 0;

    if (gflagChk(131) == 0) {
        ((GObj *)scpSearchGobj(411))->active = 0;
        ((GObj *)scpSearchGobj(412))->active = 0;
        ((GObj *)scpSearchGobj(413))->active = 0;
        ((GObj *)scpSearchGobj(414))->active = 0;
        ((GObj *)scpSearchGobj(415))->active = 0;
        ((GObj *)scpSearchGobj(416))->active = 0;

        ene_mes[0].func = actSt07aEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt07aEneChk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (gflagChk(PAL_NTSCAUG6JUN26(133, 119)) == 0 ||
           scpTriggerBall(self, girlGObj, 400.0f) == 0) {
        _ACTWait(1);
    }

    ((GObj *)scpSearchGobj(PAL_NTSC_AUG6JUN26(396, 361, 360)))->active = 1;

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
    scpBoyControlReadDisable = 1;

    scpSleepEnemyOne(PAL_JPNKORCN_NTSC_AUG6JUN26(3757, 3734, 3453, 3415));

    gflagOff(PAL_NTSC_AUG6JUN26(391, 358, 357));

    gflagOn(PAL_NTSCAUG6JUN26(131, 117));
    gflagOn(PAL_NTSCAUG6JUN26(132, 118));

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(359, 355, 325, 324), 1, 0);
    SetCameraFlag_LwsCutBack();

    reg_SetScissorSw(1);

    SetStaticBlur(0);

    scpSleepEnemyOne(PAL_NTSC_AUG6JUN26(400, 364, 363));
    scpSleepSpiderGroupOne(PAL_NTSC_AUG6JUN26(405, 369, 368));

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(359, 355, 325, 324)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    reg_SetScissorSw(0);

    SetStaticBlur(1);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));
    scpBoyControlReadDisable = 0;

    ((GObj *)scpSearchGobj(PAL_NTSC_AUG6JUN26(396, 361, 360)))->active = 0;

    _ACTWait(30);

    scpWakeupEnemyOne(PAL_JPNKORCN_NTSC_AUG6JUN26(3757, 3734, 3453, 3415));
    scpWakeupEnemyOne(PAL_NTSC_AUG6JUN26(400, 364, 363));
    scpWakeupSpiderGroupOne(PAL_NTSC_AUG6JUN26(405, 369, 368));
}

static void actSt07aGene1_ntscaug6jun26(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(118) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt07aGene1(GObj *volatile self)
{
    if (ICO_NTSCAUG6JUN26) {
        actSt07aGene1_ntscaug6jun26(self);
        return;
    }
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    Generator_Mask(scpSearchGobj(410));

    while (gflagChk(132) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_Call(scpSearchGobj(410));
}

void actSt07aChan(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(126, 113)) != 0) {
        DeleteLayoutedCandleParticleEffect();
        if (gflagChk(PAL_NTSCAUG6JUN26(128, 114)) != 0) {
            scpTorchLightOff(PAL_NTSC_AUG6JUN26(431, 394, 393));
            scpTorchLightOff(PAL_NTSC_AUG6JUN26(432, 395, 394));
        }
    } else {
        scpTorchLightOff(PAL_NTSC_AUG6JUN26(431, 394, 393));
        scpTorchLightOff(PAL_NTSC_AUG6JUN26(432, 395, 394));
        chan_mes[0].func = actSt07aChanChk;
        act->mail = chan_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt07aChanReady(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(126) == 0) {
        chanReady_mes[0].func = actSt07aChanReadyChk;
        act->mail = chanReady_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt07aChanChain(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(127) == 0) {
        chanChain_mes[0].func = actSt07aChanChainChk;
        act->mail = chanChain_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        ((GObj *)scpSearchGobj(454))->active = 0;
    }
}

void actSt07aTsuro(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(128, 114)) == 0) {
        tsuro_mes[0].func = actSt07aTsuroChk;
        act->mail = tsuro_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        gflagOn(PAL_NTSCAUG6JUN26(133, 119));
    }
}

void actSt07aIntro(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(129, 115)) == 0) {
        intro_mes[0].func = actSt07aIntroChk;
        act->mail = intro_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt07aSekizo(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(130, 116)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(362, 358, 327, 326), 0, 0);
        sekizo_mes[0].func = actSt07aSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(362, 358, 327, 326), 0, -1);
    }
}

void actSt07aGene2(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(PAL_NTSCAUG6JUN26(132, 118)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_MaskOff(self);
}

void actSt07aGene3(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(PAL_NTSCAUG6JUN26(132, 118)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_MaskOff(self);
}

void actSt07aEne2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(134, 120)) == 0) {
        ene2_mes[0].func = actSt07aEne2Chk;
        act->mail = ene2_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt07aGene2_1(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(PAL_NTSCAUG6JUN26(135, 121)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    Generator_MaskOff(self);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt07aGene2_2(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(PAL_NTSCAUG6JUN26(135, 121)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_MaskOff(self);
}

void actSt07aGene2_3(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(PAL_NTSCAUG6JUN26(135, 121)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    Generator_MaskOff(self);
}

static void actSt07aChanReadyChk(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }
    scpAdpcmPlayRequestFunc(59, &bridge, 1, 1, 0);
}

static void actSt07aChanChainChk(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(127) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(454))) {
        case 1:
            if (scpGameStat_BoyWeaponkind() != 0) {
                gflagOn(127);
                soundSeDefPlay(878, 0, 0, 1);
                _ACTWait(30);
                ((GObj *)scpSearchGobj(454))->active = 0;
            }
        case 0:
            _ACTWait(1);
            break;
        case 2:
            ((GObj *)scpSearchGobj(454))->active = 0;
            gflagOn(127);
            break;
        }
    }
}

void actSt07ChanEvent(int x)
{
    volatile int local = x;
}

void actSt07aChanFall(GObj *volatile self)
{
    if (ICO_NTSC) {
        stage_SetAnimation(322, 1, 0);
    } else if (ICO_AUG6JUN26) {
        stage_SetAnimation(321, 1, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(356, 352), 1, 0);
        _ACTWait(5);
        stage_SetAnimation(PAL_JPNKORCN(355, 351), -1, -2);
    }

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(356, 352, 322, 321)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    DeleteLayoutedCandleParticleEffect();
    _ACTWait(120);
    scpTorchLightOn(PAL_NTSC_AUG6JUN26(431, 394, 393));
    scpTorchLightOn(PAL_NTSC_AUG6JUN26(432, 395, 394));
    _ACTWait(0);
}

void actSt07aChanMot(GObj *volatile self)
{
    scpPlayMot(boyGObj, PAL_NTSCAUG6JUN26(474, 464));
    scpPlayWaitMotEnd(boyGObj);
    scpPlayMot(boyGObj, PAL_NTSCAUG6JUN26(475, 465));
    scpPlayWaitMotEnd(boyGObj);

    demoEnd = 1;
    _ACTWait(0);
}

void actSt07aChanWay1(volatile unsigned int self)
{
    long long buf[2];
    buf[0] = chanWay1Pos.d[0];
    buf[1] = chanWay1Pos.d[1];
    _SCPMoveCharactorByWay(girlGObj, 0, (float *)buf, 100.0f, 0);
    _ACTWait(0);
}

void actSt07aChanWay2(volatile unsigned int self)
{
    long long buf[2];
    buf[0] = chanWay2Pos.d[0];
    buf[1] = chanWay2Pos.d[1];
    _SCPMoveCharactorByWay(girlGObj, 0, (float *)buf, 100.0f, 0);
    _ACTWait(0);
}

void actSt07TsuroEvent(int x)
{
    volatile int local = x;
}

void actSt07aTsuroConte(GObj *volatile self)
{
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(357, 353, 323, 322), 1, 0);
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(358, 354, 324, 323), 1, 0);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(358, 354, 324, 323)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

void actSt07aGirlWay(volatile unsigned int self)
{
    long long buf[2];
    buf[0] = tsuroChkPos.d[0];
    buf[1] = tsuroChkPos.d[1];
    _SCPMoveCharactorByWay(girlGObj, 0, (float *)buf, 100.0f, 0);
    _ACTWait(0);
}

void actSt07aCamera2(int x)
{
    volatile int local = x;
}

void actSt07aIntroChk(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 100.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
    gflagOn(PAL_NTSCAUG6JUN26(129, 115));

    scpBoyControlReadDisable = 1;
    _ACTWait(1);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(353, 349, 320, 319), 1, 0);
    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(353, 349, 320, 319)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));
    scpBoyControlReadDisable = 0;
}

void actSt07aSekizoEvent(int x)
{
    volatile int local = x;
}

void actSt07aEne2Chk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (gflagChk(PAL_NTSCAUG6JUN26(174, 155)) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x4000000) == 0) {
        _ACTWait(1);
    }

    gflagOff(PAL_NTSC_AUG6JUN26(391, 358, 357));
    gflagOn(PAL_NTSCAUG6JUN26(134, 120));
    gflagOn(PAL_NTSCAUG6JUN26(135, 121));
}
