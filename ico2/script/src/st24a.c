#include "st24a.h"
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
#include <libvu0.h>
#include "typedef.h"
#include "script.h"
#include "main.h"

static void actSt24aSwordSub(GObj *volatile self);

static ActMail sword_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail sword_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail sword_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail sword_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define sword_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(sword_mes_pal, sword_mes_ntsc, sword_mes_aug6, sword_mes_jun26)

static ActMail demoCam_mes[2] = {{430}, {429}}; /* derived name */

/* .sbss: the demo's own end flag, raised by the subthread the wait loop below
   spins for. */
static int demoEnd;

static const Vec16 swordChkPos = {{1685.0f, -1080.0f, -1000.0f, 1.0f}}; /* derived name */

/* .sdata: the sword's object. */
SqEntry *sword = 0;

static void actSt24aSwordChk_ntsc(GObj *volatile self)
{
    float v[4];
    float dir[4];

    while (scpGameStat_BoyWeaponkind() != 5 || scpTriggerBall(self, boyGObj, 300.0f) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    gflagOn(300);
    soundSeDefPlay(1316, 0, 0, 1);
    _ACTWait(60);
    scpAdpcmPlayRequestFunc(33, &sword, 1, 1, 1);
    while (sword == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(133, 1, 0);
    scpPlayPosSet(boyGObj, 1685.0f, -1080.0f, -550.0f);
    *(Vec16 *)v = swordChkPos;
    sceVu0SubVector(dir, v, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    _ACTWait(300);
    scpPlayMot(boyGObj, 246);
    while (stage_CheckAnimationFinish(133) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
}

static void actSt24aSwordChk_aug6jun26(GObj *volatile self)
{
    float v[4];
    float dir[4];

    while (scpGameStat_BoyWeaponkind() != 5 || scpTriggerBall(self, boyGObj, 300.0f) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    scpPlayMot(boyGObj, 0);
    gflagOn(PAL_JUN26(299, 285));
    soundSeDefPlay(PAL_JUN26(1316, 1285), 0, 0, 1);
    _ACTWait(60);
    scpAdpcmPlayRequestFunc(PAL_JUN26(33, 36), &sword, 1, 1, 1);
    while (sword == 0) {
        _ACTWait(1);
    }
    stage_SetAnimation(PAL_JUN26(132, 119), 1, 0);
    scpPlayPosSet(boyGObj, 1685.0f, -1080.0f, -550.0f);
    *(Vec16 *)v = swordChkPos;
    sceVu0SubVector(dir, v, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    _ACTWait(300);
    scpPlayMot(boyGObj, PAL_JUN26(246, 253));
    while (stage_CheckAnimationFinish(PAL_JUN26(132, 119)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
}

void actSt24aSwordChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt24aSwordChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt24aSwordChk_aug6jun26(self);
        return;
    }
    float v[4];
    float dir[4];
    GProc *th;

    while ((GOBJ_ACT(boyGObj)->pad.trg & 0x20) == 0 || scpTriggerBall(self, boyGObj, 100.0f) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    gflagOn(329);
    soundSeDefPlay(1343, 0, 0, 1);
    scpAdpcmPlayRequestFunc(34, &sword, 1, 1, 0);
    th = actCreateSubThread(actSt24aSwordSub, 21);
    demoEnd = 0;
    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }
    iosThreadSetPri(&th->thread, 34);
    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);
        while (sword == 0) {
            _ACTWait(1);
        }
        scpAdpcmFadeCloseFunc(&sword, 512);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }
        stage_SetAnimation(PAL_JPNKORCN(153, 149), 0, -1);
        scpPlayPosSet(boyGObj, 1685.0f, -1080.0f, -550.0f);
        *(Vec16 *)v = swordChkPos;
        sceVu0SubVector(dir, v, test_CURRENTROOT(boyGObj));
        scpPlayMotDir(boyGObj, dir);
        SetCameraFlag_LwsCutBack();
        scpSetBoyWeaponGObj(scpSearchGobj(2098));
        scpFadeIn(3.0f);
    }
    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpBoyControlReadDisable = 0;
}

static void actSt24aDemoCamChk(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 700.0f) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    gflagOn(330);
    stage_SetAnimation(PAL_JPNKORCN(154, 150), 1, 0);
    SetCameraFlag_LwsCutBack();
    while (stage_CheckAnimationFinish(PAL_JPNKORCN(154, 150)) == 0) {
        if (pad[0].flags & 0x800) {
            if (scpAdpcmPlayRequestNum() == 0) {
                scpFadeOut(16.0f, 0, 0, 0);
                while (scpFadeChk() != 0) {
                    _ACTWait(1);
                }
                while (lt_fade_status() != 2) {
                    _ACTWait(1);
                }
                stage_SetAnimation(PAL_JPNKORCN(154, 150), 0, -1);
                scpFadeIn(3.0f);
                break;
            }
        }
        _ACTWait(1);
    }
    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpBoyControlReadDisable = 0;
}

static void actSt24aSword_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(285) == 0) {
        sword_mes[0].func = actSt24aSwordChk;
        act->mail = sword_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    }
}

void actSt24aSword(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt24aSword_jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    ScpCallCameraSetTarget(363.0f, 1307.0f, -3297.0f);

    if (gflagChk(PAL_NTSC_AUG6JUN26(329, 300, 299)) == 0) {
        sword_mes[0].func = actSt24aSwordChk;
        act->mail = sword_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt24aSaku(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(151, 147, 131, 130, 117), 0, 0);
}

void actSt24aDemoCam(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(330) == 0) {
        demoCam_mes[0].func = actSt24aDemoCamChk;
        act->mail = demoCam_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static void actSt24aSwordSub(GObj *volatile self)
{
    while (sword == 0) {
        _ACTWait(1);
    }
    AdpcmPlay(sword->stream);

    stage_SetAnimation(PAL_JPNKORCN(153, 149), 1, 0);

    scpPlayMot(boyGObj, 250);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(153, 149), 21, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpSetBoyWeaponGObj(scpSearchGobj(2098));

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(153, 149)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}
