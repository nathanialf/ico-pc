#include "st03t.h"
#include "debug.h"
#include "layout_texture.h"
#include "s_init.h"
#include "act.h"
#include "commonact.h"
#include "way_llf.h"
#include "camera-ico2.h"
#include "camera-root.h"
#include "generator.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "box.h"
#include "motionManager2.h"
#include "typedef.h"
#include "main.h"
#include "script.h"

/* The functions stand in the order of the stage's source (aug6's TRFILE and the
   PAL listing number them so). The ones defined inline are output at
   the end of the file, in the order of the declarations below, which gives
   this build's .text order; the others are output where they stand. The
   mail tables are file-scope statics, except girlCamEndChk_mes, a static
   local of its poster: .data keeps the source order and the table takes
   gcc's local name.N symbol. */

inline void actSt03tGirlCam(GObj *volatile self);
inline void actSt03tSekizo(GObj *volatile self);
inline void actSt03tWay(GObj *volatile self);
inline void actSt03tEne(GObj *volatile self);
inline void actSt03tGene(GObj *volatile self);
inline void actSt03tGirlCamEvent(int x);
inline void actSt03tGirlCamStartChk(GObj *volatile self);
inline void actSt03tGirlCamEndChk(GObj *volatile self);
inline void actSt03tSekizoEvent(int x);
inline void actSt03tEneChk(GObj *volatile self);
inline void actSt03tWayOnChk(GObj *volatile self);
inline void actSt03tWayOffChk(GObj *volatile self);
static void actSt03tGirlPosChk(GObj *volatile self);
static void actSt03tGirlUpChk(GObj *volatile self);
static void actSt03tHint1OffChk(GObj *volatile self);

static ActMail switchL_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail switchL_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail switchL_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define switchL_mes PAL_NTSC_AUG6(switchL_mes_pal, switchL_mes_ntsc, switchL_mes_aug6)

static ActMail switchLUp_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail switchLUp_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail switchLUp_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define switchLUp_mes PAL_NTSC_AUG6(switchLUp_mes_pal, switchLUp_mes_ntsc, switchLUp_mes_aug6)

static ActMail switchLChk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail switchLChk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail switchLChk_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define switchLChk_mes PAL_NTSC_AUG6(switchLChk_mes_pal, switchLChk_mes_ntsc, switchLChk_mes_aug6)

static ActMail switchLUpChk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail switchLUpChk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail switchLUpChk_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define switchLUpChk_mes                                                                           \
    PAL_NTSC_AUG6(switchLUpChk_mes_pal, switchLUpChk_mes_ntsc, switchLUpChk_mes_aug6)

static ActMail switchR_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail switchR_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail switchR_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define switchR_mes PAL_NTSC_AUG6(switchR_mes_pal, switchR_mes_ntsc, switchR_mes_aug6)

static ActMail switchRUp_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail switchRUp_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail switchRUp_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define switchRUp_mes PAL_NTSC_AUG6(switchRUp_mes_pal, switchRUp_mes_ntsc, switchRUp_mes_aug6)

static ActMail switchRChk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail switchRChk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail switchRChk_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define switchRChk_mes PAL_NTSC_AUG6(switchRChk_mes_pal, switchRChk_mes_ntsc, switchRChk_mes_aug6)

static ActMail switchRUpChk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail switchRUpChk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail switchRUpChk_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define switchRUpChk_mes                                                                           \
    PAL_NTSC_AUG6(switchRUpChk_mes_pal, switchRUpChk_mes_ntsc, switchRUpChk_mes_aug6)

static ActMail girlCam_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail girlCam_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail girlCam_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define girlCam_mes PAL_NTSC_AUG6(girlCam_mes_pal, girlCam_mes_ntsc, girlCam_mes_aug6)

static ActMail girlCamStartChk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail girlCamStartChk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail girlCamStartChk_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define girlCamStartChk_mes                                                                        \
    PAL_NTSC_AUG6(girlCamStartChk_mes_pal, girlCamStartChk_mes_ntsc, girlCamStartChk_mes_aug6)

static ActMail girlCamEndChk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail ene_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail ene_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail ene_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define ene_mes PAL_NTSC_AUG6(ene_mes_pal, ene_mes_ntsc, ene_mes_aug6)

static ActMail wayOn_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail wayOn_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail wayOn_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define wayOn_mes PAL_NTSC_AUG6(wayOn_mes_pal, wayOn_mes_ntsc, wayOn_mes_aug6)

static ActMail wayOff_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail wayOff_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail wayOff_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define wayOff_mes PAL_NTSC_AUG6(wayOff_mes_pal, wayOff_mes_ntsc, wayOff_mes_aug6)

static ActMail wayOnChk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail wayOnChk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail wayOnChk_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define wayOnChk_mes PAL_NTSC_AUG6(wayOnChk_mes_pal, wayOnChk_mes_ntsc, wayOnChk_mes_aug6)

static ActMail wayOffChk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail wayOffChk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail wayOffChk_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define wayOffChk_mes PAL_NTSC_AUG6(wayOffChk_mes_pal, wayOffChk_mes_ntsc, wayOffChk_mes_aug6)

static ActMail girlPos_mes[2] = {{430}, {429}}; /* derived name */

static ActMail girlUp_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hint1Sleep_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hint1OffChk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hint1OnChk_mes[2] = {{430}, {429}}; /* derived name */

void actSt03tSwitchL(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6(89, 80)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(364, 360, 329, 328), 0, 0);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(366, 362, 331, 330), 0, 0);

        switchL_mes[0].func = actSt03tSwitchLChk;
        act->mail = switchL_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(364, 360, 329, 328), 0, 45);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(366, 362, 331, 330), 0, -1);

        switchLUp_mes[0].func = actSt03tSwitchLUpChk;
        act->mail = switchLUp_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);
    }
}

static void actSt03tSwitchLChk_ntsc(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);
    int i;

    i = 0;
    while (i < (60 - systemStatus[0] * 10) / systemStatus[1]) {
        if (scpTriggerFloorAttrTargetMan(self, 0x1000000) != 0) {
            i++;
        } else {
            i = 0;
        }
        _ACTWait(1);
    }

    stage_SetAnimation(329, 1, 0);
    soundSeDefPlay(1196, 0, 0, 1);

    while (stage_CheckAnimationFrame(329, 45, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    gflagOn(80);

    stage_SetAnimation(331, 1, 0);

    SetWayGroupActive(7, 1);

    gflagOn(82);

    while (stage_CheckAnimationFrame(331, 75, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    soundSeDefPlay(1194, 0, 0, 1);

    while (stage_CheckAnimationFrame(331, 90, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    soundSeDefPlay(1195, 0, 0, 1);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(331) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    switchLChk_mes[0].func = actSt03tSwitchLUpChk;
    act->mail = switchLChk_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static void actSt03tSwitchLChk_aug6(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);
    int i;

    i = 0;
    while (i < (60 - systemStatus[0] * 10) / systemStatus[1]) {
        if (scpTriggerFloorAttrTargetMan(self, 0x1000000) != 0) {
            i++;
        } else {
            i = 0;
        }
        _ACTWait(1);
    }

    stage_SetAnimation(328, 1, 0);
    soundSeDefPlay(1196, 0, 0, 1);

    while (stage_CheckAnimationFrame(328, 45, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    gflagOn(80);

    stage_SetAnimation(330, 1, 0);

    gflagOn(82);

    while (stage_CheckAnimationFrame(330, 75, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    soundSeDefPlay(1194, 0, 0, 1);

    while (stage_CheckAnimationFrame(330, 90, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    soundSeDefPlay(1195, 0, 0, 1);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(330) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    switchLChk_mes[0].func = actSt03tSwitchLUpChk;
    act->mail = switchLChk_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt03tSwitchLChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt03tSwitchLChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt03tSwitchLChk_aug6(self);
        return;
    }
    Act *act = GOBJ_ACT(self);
    int i;

    i = 0;
    while (i < (60 - systemStatus[0] * 10) / systemStatus[1]) {
        if (scpTriggerFloorAttrTargetMan(self, 0x1000000) != 0) {
            i++;
        } else {
            i = 0;
        }
        _ACTWait(1);
    }

    gflagOn(102);

    stage_SetAnimation(PAL_JPNKORCN(364, 360), 1, 0);
    soundSeDefPlay(1220, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(364, 360), 45, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    gflagOn(89);

    stage_SetAnimation(PAL_JPNKORCN(366, 362), 1, 0);

    SetWayGroupActive(7, 1);

    gflagOn(91);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(366, 362), 75, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    soundSeDefPlay(1218, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(366, 362), 90, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    soundSeDefPlay(1219, 0, 0, 1);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(366, 362)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    switchLChk_mes[0].func = actSt03tSwitchLUpChk;
    act->mail = switchLChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt03tSwitchLUpChk(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    while (scpTriggerFloorAttrTargetMan(self, 0x1000000) != 0) {
        _ACTWait(1);
    }

    _ACTWait(60);

    gflagOff(PAL_NTSCAUG6(89, 80));

    if (ICO_NTSC) {
        stage_SetAnimation(329, 1, 46);
        soundSeDefPlay(1196, 0, 0, 1);
    } else if (ICO_AUG6) {
        stage_SetAnimation(328, 1, 46);
        soundSeDefPlay(1196, 0, 0, 1);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(364, 360), 1, 46);
        soundSeDefPlay(1220, 0, 0, 1);
    }

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6(364, 360, 329, 328), 90, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (ICO_NTSC) {
        stage_SetAnimation(332, 1, 0);

        SetWayGroupActive(7, 0);

        soundSeDefPlay(1194, 0, 0, 1);
    } else if (ICO_AUG6) {
        stage_SetAnimation(331, 1, 0);

        soundSeDefPlay(1194, 0, 0, 1);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(367, 363), 1, 0);

        if (girlGObj != 0) {
            scpCheckDisconnectWallStart(girlGObj);
        }

        SetWayGroupActive(7, 0);

        soundSeDefPlay(1218, 0, 0, 1);
    }
    _ACTWait(30);
    soundSeDefPlay(PAL_NTSCAUG6(1219, 1195), 0, 0, 1);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6(367, 363, 332, 331), 120, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (!ICO_NTSCAUG6) {
        if (girlGObj != 0) {
            scpCheckDisconnectWallEnd(girlGObj);
        }
    }

    switchLUpChk_mes[0].func = actSt03tSwitchLChk;
    act->mail = switchLUpChk_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

void actSt03tSwitchR(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6(90, 81)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(365, 361, 330, 329), 0, 0);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(368, 364, 333, 332), 0, 0);

        switchR_mes[0].func = actSt03tSwitchRChk;
        act->mail = switchR_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(365, 361, 330, 329), 0, 45);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(368, 364, 333, 332), 0, -1);

        switchRUp_mes[0].func = actSt03tSwitchRUpChk;
        act->mail = switchRUp_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);
    }
}

static void actSt03tSwitchRChk_ntsc(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);
    int i;

    i = 0;
    while (i < (60 - systemStatus[0] * 10) / systemStatus[1]) {
        if (scpTriggerFloorAttrTargetMan(self, 0x2000000) != 0) {
            i++;
        } else {
            i = 0;
        }
        _ACTWait(1);
    }

    stage_SetAnimation(330, 1, 0);
    soundSeDefPlay(1196, 0, 0, 1);

    while (stage_CheckAnimationFrame(330, 45, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    stage_SetAnimation(333, 1, 0);

    SetWayGroupActive(5, 1);

    gflagOn(82);

    soundSeDefPlay(1194, 0, 0, 1);
    _ACTWait(30);
    soundSeDefPlay(1195, 0, 0, 1);

    while (stage_CheckAnimationFinish(333) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    gflagOn(81);

    switchRChk_mes[0].func = actSt03tSwitchRUpChk;
    act->mail = switchRChk_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static void actSt03tSwitchRChk_aug6(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);
    int i;

    i = 0;
    while (i < (60 - systemStatus[0] * 10) / systemStatus[1]) {
        if (scpTriggerFloorAttrTargetMan(self, 0x2000000) != 0) {
            i++;
        } else {
            i = 0;
        }
        _ACTWait(1);
    }

    stage_SetAnimation(329, 1, 0);
    soundSeDefPlay(1196, 0, 0, 1);

    while (stage_CheckAnimationFrame(329, 45, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    stage_SetAnimation(332, 1, 0);

    SetWayGroupActive(5, 1);

    gflagOn(82);

    soundSeDefPlay(1194, 0, 0, 1);
    _ACTWait(30);
    soundSeDefPlay(1195, 0, 0, 1);

    while (stage_CheckAnimationFinish(332) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    gflagOn(81);

    switchRChk_mes[0].func = actSt03tSwitchRUpChk;
    act->mail = switchRChk_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt03tSwitchRChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt03tSwitchRChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt03tSwitchRChk_aug6(self);
        return;
    }
    Act *act = GOBJ_ACT(self);
    int i;

    i = 0;
    while (i < (60 - systemStatus[0] * 10) / systemStatus[1]) {
        if (scpTriggerFloorAttrTargetMan(self, 0x2000000) != 0) {
            i++;
        } else {
            i = 0;
        }
        _ACTWait(1);
    }

    gflagOn(102);

    stage_SetAnimation(PAL_JPNKORCN(365, 361), 1, 0);
    soundSeDefPlay(1220, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(365, 361), 45, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    stage_SetAnimation(PAL_JPNKORCN(368, 364), 1, 0);

    SetWayGroupActive(5, 1);

    gflagOn(91);

    soundSeDefPlay(1218, 0, 0, 1);
    _ACTWait(30);
    soundSeDefPlay(1219, 0, 0, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(368, 364)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    gflagOn(90);

    switchRChk_mes[0].func = actSt03tSwitchRUpChk;
    act->mail = switchRChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt03tSwitchRUpChk(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    while (scpTriggerFloorAttrTargetMan(self, 0x2000000) != 0) {
        _ACTWait(1);
    }

    _ACTWait(((60 - systemStatus[0] * 10) / systemStatus[1]) * 10);

    gflagOff(PAL_NTSCAUG6(90, 81));

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(365, 361, 330, 329), 1, 46);
    soundSeDefPlay(PAL_NTSCAUG6(1220, 1196), 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6(365, 361, 330, 329), 90, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(369, 365, 334, 333), 1, 0);

    SetWayGroupActive(5, 0);

    soundSeDefPlay(PAL_NTSCAUG6(1218, 1194), 0, 0, 1);
    _ACTWait(30);
    soundSeDefPlay(PAL_NTSCAUG6(1219, 1195), 0, 0, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6(369, 365, 334, 333)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    switchRUpChk_mes[0].func = actSt03tSwitchRChk;
    act->mail = switchRUpChk_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

static inline void actSt03tGene_ntsc(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    Generator_Mask(scpSearchGobj(818));

    while (gflagChk(94) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(20);
    Generator_Call(self);

    Generator_Call(scpSearchGobj(818));
}

static inline void actSt03tGene_aug6(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    Generator_Mask(scpSearchGobj(817));

    while (gflagChk(94) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(20);
    Generator_Call(self);

    Generator_Call(scpSearchGobj(817));
}

void actSt03tGene(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt03tGene_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt03tGene_aug6(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    Generator_Mask(scpSearchGobj(877));
    Generator_Mask(scpSearchGobj(878));

    while (gflagChk(105) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(20);
    Generator_Call(self);

    Generator_Call(scpSearchGobj(877));
    Generator_Call(scpSearchGobj(878));
}

void actSt03tBoxA(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSC_AUG6(298, 272, 271)) == 0) {
        scpSearchGobj(PAL_NTSC_AUG6(865, 806, 805))->active = 0;
    } else {
        scpSearchGobj(PAL_NTSC_AUG6(852, 793, 792))->active = 0;
        scpSearchGobj(PAL_NTSC_AUG6(863, 804, 803))->active = 0;
        scpSearchGobj(PAL_NTSC_AUG6(865, 806, 805))->active = 1;

        scpTransGObj(scpSearchGobj(PAL_NTSC_AUG6(865, 806, 805)), 0.0f, -200.0f, 0.0f);
        _ACTWait(1);
        ReInitBoxGeo(scpSearchGobj(PAL_NTSC_AUG6(865, 806, 805)));
    }
}

void actSt03tBoxB(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSC_AUG6(299, 273, 272)) == 0) {
        scpSearchGobj(PAL_NTSC_AUG6(866, 807, 806))->active = 0;
    } else {
        scpSearchGobj(PAL_NTSC_AUG6(853, 794, 793))->active = 0;
        scpSearchGobj(PAL_NTSC_AUG6(864, 805, 804))->active = 0;
        scpSearchGobj(PAL_NTSC_AUG6(866, 807, 806))->active = 1;

        gflagOn(PAL_NTSCAUG6(98, 89));

        scpTransGObj(scpSearchGobj(PAL_NTSC_AUG6(866, 807, 806)), 0.0f, -400.0f, 0.0f);
        _ACTWait(1);
        ReInitBoxGeo(scpSearchGobj(PAL_NTSC_AUG6(866, 807, 806)));
    }
}

static void actSt03tInit_ntsc(void)
{
    if (gflagChk(82) != 0) {
        SetWayGroupActive(7, 1);
    } else {
        SetWayGroupActive(7, 0);
    }

    if (gflagChk(83) != 0) {
        SetWayGroupActive(5, 1);
    } else {
        SetWayGroupActive(5, 0);
    }
}

static void actSt03tInit_aug6(void)
{
    if (gflagChk(82) == 0) {
        SetWayGroupActive(7, 0);
    }

    if (gflagChk(83) != 0) {
        SetWayGroupActive(5, 1);
    } else {
        SetWayGroupActive(5, 0);
    }
}

void actSt03tInit(void)
{
    if (ICO_NTSC) {
        actSt03tInit_ntsc();
        return;
    }
    if (ICO_AUG6) {
        actSt03tInit_aug6();
        return;
    }
    if (gflagChk(91) != 0) {
        SetWayGroupActive(7, 1);
    } else {
        SetWayGroupActive(7, 0);
    }

    if (gflagChk(92) != 0) {
        SetWayGroupActive(5, 1);
    } else {
        SetWayGroupActive(5, 0);
    }
}

void actSt03tGirlUp(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(93) == 0) {
        girlUp_mes[0].func = actSt03tGirlUpChk;
        act->mail = girlUp_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        FinishHint(12);
    }
}

static inline void actSt03tGirlCam_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    girlCam_mes[0].func = actSt03tGirlCamStartChk;
    act->mail = girlCam_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt03tGirlCam_aug6(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    girlCam_mes[0].func = actSt03tGirlCamStartChk;
    act->mail = girlCam_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt03tGirlCam(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt03tGirlCam_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt03tGirlCam_aug6(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    girlCam_mes[0].func = actSt03tGirlCamStartChk;
    act->mail = girlCam_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static inline void actSt03tSekizo_ntscaug6(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    scpSekizou(self, 86, 67, 0, 18, -913.0f, -400.0f, 605.0f, -1000.0f, -400.0f, 550.0f);
}

void actSt03tSekizo(GObj *volatile self)
{
    if (ICO_NTSCAUG6) {
        actSt03tSekizo_ntscaug6(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    scpSekizou(self, 95, PAL_JPNKORCN(80, 76), 0, 18, -913.0f, -400.0f, 605.0f, -1000.0f, -400.0f,
               550.0f);
}

static inline void actSt03tWay_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(91) == 0) {
        wayOn_mes[0].func = actSt03tWayOnChk;
        act->mail = wayOn_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        wayOff_mes[0].func = actSt03tWayOffChk;
        act->mail = wayOff_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt03tWay_aug6(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(91) == 0) {
        wayOn_mes[0].func = actSt03tWayOnChk;
        act->mail = wayOn_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    } else {
        wayOff_mes[0].func = actSt03tWayOffChk;
        act->mail = wayOff_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt03tWay(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt03tWay_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt03tWay_aug6(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(100) == 0) {
        wayOn_mes[0].func = actSt03tWayOnChk;
        act->mail = wayOn_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        wayOff_mes[0].func = actSt03tWayOffChk;
        act->mail = wayOff_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static inline void actSt03tEne_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(93) == 0) {
        ene_mes[0].func = actSt03tEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt03tEne_aug6(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(93) == 0) {
        ene_mes[0].func = actSt03tEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt03tEne(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt03tEne_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt03tEne_aug6(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(104) == 0) {
        ene_mes[0].func = actSt03tEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt03tGirlPos(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(101) == 0) {
        SleepHint(12);

        girlPos_mes[0].func = actSt03tGirlPosChk;
        act->mail = girlPos_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt03tHint1Sleep(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    hint1Sleep_mes[0].func = actSt03tHint1OffChk;
    act->mail = hint1Sleep_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt03tGirlCamEvent(int x)
{
    volatile int local = x;
}

static inline void actSt03tGirlCamStartChk_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerBall(self, boyGObj, 100.0f) == 0 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) != 198) {
        _ACTWait(1);
    }

    CameraGetTarget();
    Camctrl_SetTarget(girlGObj, 0, 3);
    _ACTWait(15);
    CameraSetCameraSet(48);

    girlCamStartChk_mes[0].func = actSt03tGirlCamEndChk;
    sub->mail = girlCamStartChk_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt03tGirlCamStartChk_aug6(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerBall(self, boyGObj, 100.0f) == 0 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) != 198) {
        _ACTWait(1);
    }

    CameraGetTarget();
    Camctrl_SetTarget(girlGObj, 0, 3);
    _ACTWait(15);
    CameraSetCameraSet(48);

    girlCamStartChk_mes[0].func = actSt03tGirlCamEndChk;
    sub->mail = girlCamStartChk_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt03tGirlCamStartChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt03tGirlCamStartChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt03tGirlCamStartChk_aug6(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerBall(self, boyGObj, 100.0f) == 0 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) != 202) {
        _ACTWait(1);
    }

    CameraGetTarget();
    Camctrl_SetTarget(girlGObj, 0, 3);
    _ACTWait(15);
    CameraSetCameraSet(48);

    girlCamStartChk_mes[0].func = actSt03tGirlCamEndChk;
    sub->mail = girlCamStartChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static inline void actSt03tGirlCamEndChk_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    static ActMail girlCamEndChk_mes[2] = {{397}, {396}}; /* derived name */

    while (scpTriggerBall(self, boyGObj, 100.0f) == 0 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) == 198) {
        _ACTWait(1);
    }

    CameraGetTarget();
    _ACTWait(90);
    CameraSetCameraSet_Default();
    Camctrl_SetTarget(boyGObj, 0, 3);

    girlCamEndChk_mes[0].func = actSt03tGirlCamStartChk;
    sub->mail = girlCamEndChk_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt03tGirlCamEndChk_aug6(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    static ActMail girlCamEndChk_mes[2] = {{393}, {392}}; /* derived name */

    while (scpTriggerBall(self, boyGObj, 100.0f) == 0 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) == 198) {
        _ACTWait(1);
    }

    CameraGetTarget();
    _ACTWait(90);
    CameraSetCameraSet_Default();
    Camctrl_SetTarget(boyGObj, 0, 3);

    girlCamEndChk_mes[0].func = actSt03tGirlCamStartChk;
    sub->mail = girlCamEndChk_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt03tGirlCamEndChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt03tGirlCamEndChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt03tGirlCamEndChk_aug6(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerBall(self, boyGObj, 100.0f) == 0 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) == 202) {
        _ACTWait(1);
    }

    CameraGetTarget();
    _ACTWait(90);
    CameraSetCameraSet_Default();
    Camctrl_ExitEveRock();

    girlCamEndChk_mes[0].func = actSt03tGirlCamStartChk;
    sub->mail = girlCamEndChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt03tSekizoEvent(int x)
{
    volatile int local = x;
}

static inline void actSt03tEneChk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerBall(self, boyGObj, 100.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyOne(3453);
    gflagOn(93);
    gflagOn(94);
    stage_SetAnimation(68, 1, 0);

    while (stage_CheckAnimationFinish(68) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyOne(3453);
}

static inline void actSt03tEneChk_aug6(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerBall(self, boyGObj, 100.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyOne(3415);
    gflagOn(93);
    gflagOn(94);
    stage_SetAnimation(68, 1, 0);

    while (stage_CheckAnimationFinish(68) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyOne(3415);
}

void actSt03tEneChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt03tEneChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt03tEneChk_aug6(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerBall(self, boyGObj, 100.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyOne(PAL_JPNKORCN(3757, 3734));
    gflagOn(104);
    gflagOn(105);
    stage_SetAnimation(PAL_JPNKORCN(81, 77), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(81, 77)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyOne(PAL_JPNKORCN(3757, 3734));
}

static inline void actSt03tWayOnChk_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpCheckExistAliveEnemy() != 0 || scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(17, 1);
    gflagOn(91);

    wayOnChk_mes[0].func = actSt03tWayOffChk;
    sub->mail = wayOnChk_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt03tWayOnChk_aug6(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(17, 1);
    gflagOn(91);

    wayOnChk_mes[0].func = actSt03tWayOffChk;
    sub->mail = wayOnChk_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt03tWayOnChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt03tWayOnChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt03tWayOnChk_aug6(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpCheckExistAliveEnemy() != 0 || scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(17, 1);
    gflagOn(100);

    wayOnChk_mes[0].func = actSt03tWayOffChk;
    sub->mail = wayOnChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static inline void actSt03tWayOffChk_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpCheckExistAliveEnemy() == 0 && scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(17, 0);
    gflagOff(91);

    wayOffChk_mes[0].func = actSt03tWayOnChk;
    sub->mail = wayOffChk_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt03tWayOffChk_aug6(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(17, 0);
    gflagOff(91);

    wayOffChk_mes[0].func = actSt03tWayOnChk;
    sub->mail = wayOffChk_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt03tWayOffChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt03tWayOffChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt03tWayOffChk_aug6(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpCheckExistAliveEnemy() == 0 && scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(17, 0);
    gflagOff(100);

    wayOffChk_mes[0].func = actSt03tWayOnChk;
    sub->mail = wayOffChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt03tGirlPosChk(GObj *volatile self)
{
    while (girlGObj == 0 || scpTriggerFloorAttr(girlGObj, 0x5000000) == 0) {
        _ACTWait(1);
    }

    gflagOn(101);
    WakeupHint(12);
}

static void actSt03tGirlUpChk(GObj *volatile self)
{
    while (girlGObj == 0 || scpTriggerFloorAttr(girlGObj, 0x4000000) == 0) {
        _ACTWait(1);
    }

    debug_StdPrintfDummy("HINT1_FINISH!!!!!!!!!!!!!!!\n");
    gflagOn(93);
    FinishHint(12);
}

static void actSt03tHint1OnChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 60);
    WakeupHint(12);

    hint1OnChk_mes[0].func = actSt03tHint1OffChk;
    sub->mail = hint1OnChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt03tHint1OffChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (gflagChk(102) == 0) {
        _ACTWait(1);
    }

    SleepHint(12);
    gflagOff(102);

    hint1OffChk_mes[0].func = actSt03tHint1OnChk;
    sub->mail = hint1OffChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}
