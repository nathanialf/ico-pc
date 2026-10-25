#include "st99a.h"
#include "act.h"
#include "commonact.h"
#include "script.h"
#include "StageAnimation.h"
#include "typedef.h"
#include "main.h"

static void actSt17aTestChk(GObj *volatile self);

static ActMail explode_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail explode_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail explode_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail explode_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define explode_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(explode_mes_pal, explode_mes_ntsc, explode_mes_aug6, explode_mes_jun26)

static ActMail splash1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail splash1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail splash1_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail splash1_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define splash1_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(splash1_mes_pal, splash1_mes_ntsc, splash1_mes_aug6, splash1_mes_jun26)

static ActMail splash2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail splash2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail splash2_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail splash2_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define splash2_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(splash2_mes_pal, splash2_mes_ntsc, splash2_mes_aug6, splash2_mes_jun26)

static ActMail wave_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail wave_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail wave_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail wave_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define wave_mes PAL_NTSC_AUG6_JUN26(wave_mes_pal, wave_mes_ntsc, wave_mes_aug6, wave_mes_jun26)

static ActMail st27aWave_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail st27aWave_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail st27aWave_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail st27aWave_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define st27aWave_mes                                                                              \
    PAL_NTSC_AUG6_JUN26(st27aWave_mes_pal, st27aWave_mes_ntsc, st27aWave_mes_aug6,                 \
                        st27aWave_mes_jun26)

static ActMail spider_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail spider_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail spider_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail spider_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define spider_mes                                                                                 \
    PAL_NTSC_AUG6_JUN26(spider_mes_pal, spider_mes_ntsc, spider_mes_aug6, spider_mes_jun26)

static ActMail st17aTest_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail st17aTest_mes_ntsc[2] = {{397}, {396}}; /* derived name */

#define st17aTest_mes PAL_NTSC(st17aTest_mes_pal, st17aTest_mes_ntsc)

void actExplode(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    explode_mes[0].func = actExplodeChk;
    act->mail = explode_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

void actSplash1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    splash1_mes[0].func = actSplash1Chk;
    act->mail = splash1_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

void actSplash2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    splash2_mes[0].func = actSplash2Chk;
    act->mail = splash2_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

void actWave(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    wave_mes[0].func = actWaveChk;
    act->mail = wave_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

void actSpider(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    stage_SetLoopFlag(PAL_JPNKORCN_NTSC_AUG6_JUN26(160, 156, 139, 138, 125), 1);
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(160, 156, 139, 138, 125), 1, 0);

    spider_mes[0].func = actSpiderChk;
    act->mail = spider_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

void actDevilLightning(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    scpLinkBGAtoLayoutedTargetSkeltonWithLocalRotationFlag(
        PAL_JPNKORCN_NTSC_AUG6JUN26(3513, 3490, 3242, 3212), 0,
        PAL_JPNKORCN_NTSC_AUG6JUN26(554, 550, 483, 482), 0);
}

void actQueenLightning(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    scpLinkBGAtoLayoutedTargetSkeltonWithLocalRotationFlag(
        PAL_JPNKORCN_NTSC_AUG6JUN26(3512, 3489, 3241, 3211), 0,
        PAL_JPNKORCN_NTSC_AUG6JUN26(555, 551, 484, 483), 0);
}

void actSt17aTest(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    stage_SetAnimation(PAL_JPNKORCN_NTSC(132, 128, 112), 0, 0);

    st17aTest_mes[0].func = actSt17aTestChk;
    act->mail = st17aTest_mes;
    ACTSendMailCorrect(self, PAL_NTSC(430, 397));
    _ACTWait(0);
}

void actSt27aWave(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    st27aWave_mes[0].func = actSt27aWaveChk;
    act->mail = st27aWave_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

static void actExplodeChk_ntsc(GObj *volatile self)
{
    GOBJ_SUB(scpSearchGobj(2791))->ctrl.noStand = 1;
    GOBJ_SUB(scpSearchGobj(2791))->ctrl.noStand = 0;
    scpGetWallCollision(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    _ACTWait(60);
    stage_SetAnimation(440, 1, 0);
    scpLinkBGAtoLayoutedTarget(2791, 440);
    while (stage_CheckAnimationFinish(440) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

static void actExplodeChk_jun26(GObj *volatile self)
{
    GOBJ_SUB(scpSearchGobj(2613))->ctrl.noStand = 1;
    GOBJ_SUB(scpSearchGobj(2613))->ctrl.noStand = 0;
    scpGetWallCollision(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    _ACTWait(60);
    stage_SetAnimation(403, 1, 0);
    scpLinkBGAtoLayoutedTarget(2613, 403);
    while (stage_CheckAnimationFinish(403) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

static void actExplodeChk_aug6(GObj *volatile self)
{
    GOBJ_SUB(scpSearchGobj(2761))->ctrl.noStand = 1;
    GOBJ_SUB(scpSearchGobj(2761))->ctrl.noStand = 0;
    scpGetWallCollision(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    _ACTWait(60);
    stage_SetAnimation(439, 1, 0);
    scpLinkBGAtoLayoutedTarget(2761, 439);
    while (stage_CheckAnimationFinish(439) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

void actExplodeChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actExplodeChk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actExplodeChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actExplodeChk_aug6(self);
        return;
    }
    GOBJ_SUB(scpSearchGobj(PAL_JPNKORCN(3014, 3010)))->ctrl.noStand = 1;
    GOBJ_SUB(scpSearchGobj(PAL_JPNKORCN(3014, 3010)))->ctrl.noStand = 0;
    scpGetWallCollision(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    _ACTWait(60);
    stage_SetAnimation(PAL_JPNKORCN(511, 507), 1, 0);
    scpLinkBGAtoLayoutedTarget(PAL_JPNKORCN(3014, 3010), PAL_JPNKORCN(511, 507));
    while (stage_CheckAnimationFinish(PAL_JPNKORCN(511, 507)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

static void actSplash1Chk_ntsc(GObj *volatile self)
{
    GOBJ_SUB(scpSearchGobj(2792))->ctrl.noStand = 1;
    GOBJ_SUB(scpSearchGobj(2792))->ctrl.noStand = 0;
    scpGetWallCollision(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    _ACTWait(60);
    stage_SetAnimation(428, 1, 0);
    scpLinkBGAtoLayoutedTarget(2792, 428);
    while (stage_CheckAnimationFinish(428) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

static void actSplash1Chk_jun26(GObj *volatile self)
{
    GOBJ_SUB(scpSearchGobj(2614))->ctrl.noStand = 1;
    GOBJ_SUB(scpSearchGobj(2614))->ctrl.noStand = 0;
    scpGetWallCollision(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    _ACTWait(60);
    stage_SetAnimation(391, 1, 0);
    scpLinkBGAtoLayoutedTarget(2614, 391);
    while (stage_CheckAnimationFinish(391) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

static void actSplash1Chk_aug6(GObj *volatile self)
{
    GOBJ_SUB(scpSearchGobj(2762))->ctrl.noStand = 1;
    GOBJ_SUB(scpSearchGobj(2762))->ctrl.noStand = 0;
    scpGetWallCollision(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    _ACTWait(60);
    stage_SetAnimation(427, 1, 0);
    scpLinkBGAtoLayoutedTarget(2762, 427);
    while (stage_CheckAnimationFinish(427) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

void actSplash1Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSplash1Chk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSplash1Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSplash1Chk_aug6(self);
        return;
    }
    GOBJ_SUB(scpSearchGobj(PAL_JPNKORCN(3015, 3011)))->ctrl.noStand = 1;
    GOBJ_SUB(scpSearchGobj(PAL_JPNKORCN(3015, 3011)))->ctrl.noStand = 0;
    scpGetWallCollision(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    _ACTWait(60);
    stage_SetAnimation(PAL_JPNKORCN(498, 494), 1, 0);
    scpLinkBGAtoLayoutedTarget(PAL_JPNKORCN(3015, 3011), PAL_JPNKORCN(498, 494));
    while (stage_CheckAnimationFinish(PAL_JPNKORCN(498, 494)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

static void actSplash2Chk_ntsc(GObj *volatile self)
{
    GOBJ_SUB(scpSearchGobj(2793))->ctrl.noStand = 1;
    GOBJ_SUB(scpSearchGobj(2793))->ctrl.noStand = 0;
    scpGetWallCollision(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    _ACTWait(60);
    stage_SetAnimation(429, 1, 0);
    scpLinkBGAtoLayoutedTarget(2793, 429);
    while (stage_CheckAnimationFinish(429) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

static void actSplash2Chk_jun26(GObj *volatile self)
{
    GOBJ_SUB(scpSearchGobj(2615))->ctrl.noStand = 1;
    GOBJ_SUB(scpSearchGobj(2615))->ctrl.noStand = 0;
    scpGetWallCollision(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    _ACTWait(60);
    stage_SetAnimation(392, 1, 0);
    scpLinkBGAtoLayoutedTarget(2615, 392);
    while (stage_CheckAnimationFinish(392) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

static void actSplash2Chk_aug6(GObj *volatile self)
{
    GOBJ_SUB(scpSearchGobj(2763))->ctrl.noStand = 1;
    GOBJ_SUB(scpSearchGobj(2763))->ctrl.noStand = 0;
    scpGetWallCollision(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    _ACTWait(60);
    stage_SetAnimation(428, 1, 0);
    scpLinkBGAtoLayoutedTarget(2763, 428);
    while (stage_CheckAnimationFinish(428) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

void actSplash2Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSplash2Chk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSplash2Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSplash2Chk_aug6(self);
        return;
    }
    GOBJ_SUB(scpSearchGobj(PAL_JPNKORCN(3016, 3012)))->ctrl.noStand = 1;
    GOBJ_SUB(scpSearchGobj(PAL_JPNKORCN(3016, 3012)))->ctrl.noStand = 0;
    scpGetWallCollision(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 100.0f);
    _ACTWait(60);
    stage_SetAnimation(PAL_JPNKORCN(499, 495), 1, 0);
    scpLinkBGAtoLayoutedTarget(PAL_JPNKORCN(3016, 3012), PAL_JPNKORCN(499, 495));
    while (stage_CheckAnimationFinish(PAL_JPNKORCN(499, 495)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

void actWaveChk(GObj *volatile self)
{
    actCreateSubThread(actWave1, 21);
}

void actWave1(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    while (1) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(519, 515, 448, 447, 410), 1, 0);
        _ACTWait(179);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(520, 516, 449, 448, 411), 1, 0);
        _ACTWait(179);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(521, 517, 450, 449, 412), 1, 0);
        _ACTWait(179);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(522, 518, 451, 450, 413), 1, 0);
        _ACTWait(179);
    }
}

void actSt27aWaveChk(GObj *volatile self)
{
    actCreateSubThread(actSt27aWave1, 21);
}

void actSt27aWave1(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    while (1) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(194, 190, 173, 172, 156), 1, 0);
        _ACTWait(PAL_JUN26(200, 179));
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(195, 191, 174, 173, 157), 1, 0);
        _ACTWait(PAL_JUN26(200, 179));
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(196, 192, 175, 174, 158), 1, 0);
        _ACTWait(PAL_JUN26(200, 179));
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(197, 193, 176, 175, 159), 1, 0);
        _ACTWait(PAL_JUN26(200, 179));
    }
}

void actSpiderChk(GObj *volatile self)
{
    while (1) {
        while ((GOBJ_ACT(boyGObj)->pad.trg & 0x400) == 0) {
            _ACTWait(1);
        }
        scpBornSpider(2, 0.0f, -500.0f, 0.0f, 500.0f);
        _ACTWait(1);
    }
}

static void actSt17aTestChk(GObj *volatile self)
{
    while (1) {
        while ((pad[1].flags & 0x20) == 0) {
            _ACTWait(1);
        }
        stage_SetAnimation(PAL_JPNKORCN_NTSC(84, 80, 71), 1, 0);
        _ACTWait(1);
    }
}
