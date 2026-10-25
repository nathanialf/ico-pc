#include "st18a.h"
#include "layout_texture.h"
#include "thread.h"
#include "s_init.h"
#include "act.h"
#include "gobj_process.h"
#include "commonact.h"
#include "camera-root.h"
#include "generator.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "script.h"
#include "StageAnimation.h"
#include "st04r.h"
#include "typedef.h"
#include "main.h"

/* The functions stand in the order of the stage's source (aug6's TRFILE and the
   PAL listing number them so). The ones defined inline are output at
   the end of the file, in the order of the declarations below, which gives
   this build's .text order; the others are output where they stand. The mail
   tables are file-scope statics ahead of the functions that post them, but
   for actSt18aDoorChk's own table (doorChk_mes), a static local, so .data
   keeps the source order and the table takes gcc's local name.N symbol. */

inline void actSt18aIntro(GObj *volatile self);
inline void actSt18aDoor(GObj *volatile self);
inline void actSt18aSwitchL(GObj *volatile self);
inline void actSt18aSwitchR(GObj *volatile self);
inline void actSt18aEne(GObj *volatile self);
inline void actSt18aEnemy1_2(GObj *volatile self);
inline void actSt18aEne2(GObj *volatile self);
inline void actSt18aEnemy2_2(GObj *volatile self);
inline void actSt18aCamera(int x);
inline void actSt18aIntroChk(GObj *volatile self);
inline void actSt18aEneChk(GObj *volatile self);
inline void actSt18aEne2Chk(GObj *volatile self);
static void actSt18aDoorChkSub(GObj *volatile self);

static ActMail intro_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail intro_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail intro_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define intro_mes PAL_NTSC_AUG6JUN26(intro_mes_pal, intro_mes_ntsc, intro_mes_aug6jun26)

static ActMail switchL_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail switchL_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail switchL_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define switchL_mes PAL_NTSC_AUG6JUN26(switchL_mes_pal, switchL_mes_ntsc, switchL_mes_aug6jun26)

static ActMail switchLUp_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail switchLUp_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail switchLUp_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define switchLUp_mes                                                                              \
    PAL_NTSC_AUG6JUN26(switchLUp_mes_pal, switchLUp_mes_ntsc, switchLUp_mes_aug6jun26)

static ActMail switchLChk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail switchLChk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail switchLChk_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define switchLChk_mes                                                                             \
    PAL_NTSC_AUG6JUN26(switchLChk_mes_pal, switchLChk_mes_ntsc, switchLChk_mes_aug6jun26)

static ActMail switchLUpChk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail switchLUpChk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail switchLUpChk_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define switchLUpChk_mes                                                                           \
    PAL_NTSC_AUG6JUN26(switchLUpChk_mes_pal, switchLUpChk_mes_ntsc, switchLUpChk_mes_aug6jun26)

static ActMail switchR_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail switchR_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail switchR_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define switchR_mes PAL_NTSC_AUG6JUN26(switchR_mes_pal, switchR_mes_ntsc, switchR_mes_aug6jun26)

static ActMail switchRUp_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail switchRUp_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail switchRUp_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define switchRUp_mes                                                                              \
    PAL_NTSC_AUG6JUN26(switchRUp_mes_pal, switchRUp_mes_ntsc, switchRUp_mes_aug6jun26)

static ActMail switchRChk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail switchRChk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail switchRChk_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define switchRChk_mes                                                                             \
    PAL_NTSC_AUG6JUN26(switchRChk_mes_pal, switchRChk_mes_ntsc, switchRChk_mes_aug6jun26)

static ActMail switchRUpChk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail switchRUpChk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail switchRUpChk_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define switchRUpChk_mes                                                                           \
    PAL_NTSC_AUG6JUN26(switchRUpChk_mes_pal, switchRUpChk_mes_ntsc, switchRUpChk_mes_aug6jun26)

static ActMail door_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define door_mes PAL_NTSC_AUG6JUN26(door_mes_pal, door_mes_ntsc, door_mes_aug6jun26)

static ActMail doorDown_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail doorDown_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail doorDown_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define doorDown_mes PAL_NTSC_AUG6JUN26(doorDown_mes_pal, doorDown_mes_ntsc, doorDown_mes_aug6jun26)

static ActMail doorChk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail doorDownChk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail doorDownChk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail doorDownChk_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define doorDownChk_mes                                                                            \
    PAL_NTSC_AUG6JUN26(doorDownChk_mes_pal, doorDownChk_mes_ntsc, doorDownChk_mes_aug6jun26)

static ActMail ene_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail ene_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail ene_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define ene_mes PAL_NTSC_AUG6JUN26(ene_mes_pal, ene_mes_ntsc, ene_mes_aug6jun26)

static ActMail ene2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail ene2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail ene2_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define ene2_mes PAL_NTSC_AUG6JUN26(ene2_mes_pal, ene2_mes_ntsc, ene2_mes_aug6jun26)

void actSt18aEnd(void)
{
    if (girlGObj != 0) {
        if (gflagChk(PAL_NTSCAUG6JUN26(61, 57)) == 0) {
            gflagOn(PAL_NTSC_AUG6JUN26(391, 358, 357));
        }
    }
}

static inline void actSt18aIntroChk_ntsc(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 1000.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    gflagOn(51);

    scpBoyControlReadDisable = 1;

    _ACTWait(1);

    stage_SetAnimation(321, 1, 0);

    while (stage_CheckAnimationFinish(321) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
}

static inline void actSt18aIntroChk_aug6jun26(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 1000.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    gflagOn(51);

    scpBoyControlReadDisable = 1;

    _ACTWait(1);

    stage_SetAnimation(320, 1, 0);

    while (stage_CheckAnimationFinish(320) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
}

void actSt18aIntroChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt18aIntroChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt18aIntroChk_aug6jun26(self);
        return;
    }
    while (scpTriggerBall(self, boyGObj, 1000.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));

    gflagOn(55);

    scpBoyControlReadDisable = 1;

    _ACTWait(1);

    stage_SetAnimation(PAL_JPNKORCN(354, 350), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(354, 350)) == 0) {
        if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
            scpFadeOut(16.0f, 0, 0, 0);

            while (scpFadeChk() != 0) {
                _ACTWait(1);
            }
            while (lt_fade_status() != 2) {
                _ACTWait(1);
            }

            stage_SetAnimation(PAL_JPNKORCN(354, 350), 1, -1);
            SetCameraFlag_LwsCutBack();
            scpFadeIn(3.0f);
            break;
        }
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpBoyControlReadDisable = 0;
}

static void actSt18aSwitchLChk_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    int i;

    i = 0;
    while (i < (60 - systemStatus[0] * 10) / systemStatus[1]) {
        if (scpTriggerFloorAttr(scpSearchGobj(723), 0x1000000) != 0 ||
            scpTriggerFloorAttr(scpSearchGobj(724), 0x1000000) != 0 ||
            scpTriggerFloorAttr(boyGObj, 0x1000000) != 0 ||
            (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x1000000) != 0)) {
            i++;
        } else {
            i = 0;
        }
        _ACTWait(1);
    }

    stage_SetAnimation(101, 1, 0);

    soundSeDefPlay(1196, 0, 0, 1);

    while (stage_CheckAnimationFrame(101, 45, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (scpTriggerFloorAttr(scpSearchGobj(723), 0x1000000) != 0 ||
        scpTriggerFloorAttr(scpSearchGobj(724), 0x1000000) != 0 ||
        scpTriggerFloorAttr(boyGObj, 0x1000000) != 0 ||
        scpTriggerFloorAttr(girlGObj, 0x1000000) != 0) {
        gflagOn(54);
    }

    switchLChk_mes[0].func = actSt18aSwitchLUpChk;
    sub->mail = switchLChk_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static void actSt18aSwitchLChk_aug6jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    int i;

    i = 0;
    while (i < (60 - systemStatus[0] * 10) / systemStatus[1]) {
        if (scpTriggerFloorAttr(scpSearchGobj(722), 0x1000000) != 0 ||
            scpTriggerFloorAttr(scpSearchGobj(723), 0x1000000) != 0 ||
            scpTriggerFloorAttr(boyGObj, 0x1000000) != 0 ||
            (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x1000000) != 0)) {
            i++;
        } else {
            i = 0;
        }
        _ACTWait(1);
    }

    stage_SetAnimation(100, 1, 0);

    soundSeDefPlay(1196, 0, 0, 1);

    while (stage_CheckAnimationFrame(100, 45, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (scpTriggerFloorAttr(scpSearchGobj(722), 0x1000000) != 0 ||
        scpTriggerFloorAttr(scpSearchGobj(723), 0x1000000) != 0 ||
        scpTriggerFloorAttr(boyGObj, 0x1000000) != 0 ||
        scpTriggerFloorAttr(girlGObj, 0x1000000) != 0) {
        gflagOn(54);
    }

    switchLChk_mes[0].func = actSt18aSwitchLUpChk;
    sub->mail = switchLChk_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt18aSwitchLChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt18aSwitchLChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt18aSwitchLChk_aug6jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);
    int i;

    i = 0;
    while (i < (60 - systemStatus[0] * 10) / systemStatus[1]) {
        if (scpTriggerFloorAttr(scpSearchGobj(773), 0x1000000) != 0 ||
            scpTriggerFloorAttr(scpSearchGobj(774), 0x1000000) != 0 ||
            scpTriggerFloorAttr(boyGObj, 0x1000000) != 0 ||
            (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x1000000) != 0)) {
            i++;
        } else {
            i = 0;
        }
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JPNKORCN(121, 117), 1, 0);

    soundSeDefPlay(1220, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(121, 117), 45, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (scpTriggerFloorAttr(scpSearchGobj(773), 0x1000000) != 0 ||
        scpTriggerFloorAttr(scpSearchGobj(774), 0x1000000) != 0 ||
        scpTriggerFloorAttr(boyGObj, 0x1000000) != 0 ||
        (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x1000000) != 0)) {
        gflagOn(58);
        FinishHint(7);
    }

    switchLChk_mes[0].func = actSt18aSwitchLUpChk;
    sub->mail = switchLChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt18aSwitchLUpChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttrTargetMan(self, 0x1000000) != 0) {
        _ACTWait(1);
    }

    _ACTWait(15);
    gflagOff(PAL_NTSCAUG6JUN26(58, 54));

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(121, 117, 101, 100), 1, 46);
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1220, 1196), 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6JUN26(121, 117, 101, 100), 50, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupItemWithBoundary(3054.0f, 1530.0f, -3061.0f, 100.0f);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6JUN26(121, 117, 101, 100), 90, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    switchLUpChk_mes[0].func = actSt18aSwitchLChk;
    sub->mail = switchLUpChk_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

static void actSt18aSwitchRChk_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    int i;

    i = 0;
    while (i < (60 - systemStatus[0] * 10) / systemStatus[1]) {
        if (scpTriggerFloorAttr(scpSearchGobj(723), 0x2000000) != 0 ||
            scpTriggerFloorAttr(scpSearchGobj(724), 0x2000000) != 0 ||
            scpTriggerFloorAttr(boyGObj, 0x2000000) != 0 ||
            (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x2000000) != 0)) {
            i++;
        } else {
            i = 0;
        }
        _ACTWait(1);
    }

    stage_SetAnimation(102, 1, 0);

    soundSeDefPlay(1196, 0, 0, 1);

    while (stage_CheckAnimationFrame(102, 45, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (scpTriggerFloorAttr(scpSearchGobj(723), 0x2000000) != 0 ||
        scpTriggerFloorAttr(scpSearchGobj(724), 0x2000000) != 0 ||
        scpTriggerFloorAttr(boyGObj, 0x2000000) != 0 ||
        scpTriggerFloorAttr(girlGObj, 0x2000000) != 0) {
        gflagOn(55);
    }

    switchRChk_mes[0].func = actSt18aSwitchRUpChk;
    sub->mail = switchRChk_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static void actSt18aSwitchRChk_aug6jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    int i;

    i = 0;
    while (i < (60 - systemStatus[0] * 10) / systemStatus[1]) {
        if (scpTriggerFloorAttr(scpSearchGobj(722), 0x2000000) != 0 ||
            scpTriggerFloorAttr(scpSearchGobj(723), 0x2000000) != 0 ||
            scpTriggerFloorAttr(boyGObj, 0x2000000) != 0 ||
            (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x2000000) != 0)) {
            i++;
        } else {
            i = 0;
        }
        _ACTWait(1);
    }

    stage_SetAnimation(101, 1, 0);

    soundSeDefPlay(1196, 0, 0, 1);

    while (stage_CheckAnimationFrame(101, 45, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (scpTriggerFloorAttr(scpSearchGobj(722), 0x2000000) != 0 ||
        scpTriggerFloorAttr(scpSearchGobj(723), 0x2000000) != 0 ||
        scpTriggerFloorAttr(boyGObj, 0x2000000) != 0 ||
        scpTriggerFloorAttr(girlGObj, 0x2000000) != 0) {
        gflagOn(55);
    }

    switchRChk_mes[0].func = actSt18aSwitchRUpChk;
    sub->mail = switchRChk_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt18aSwitchRChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt18aSwitchRChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt18aSwitchRChk_aug6jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);
    int i;

    i = 0;
    while (i < (60 - systemStatus[0] * 10) / systemStatus[1]) {
        if (scpTriggerFloorAttr(scpSearchGobj(773), 0x2000000) != 0 ||
            scpTriggerFloorAttr(scpSearchGobj(774), 0x2000000) != 0 ||
            scpTriggerFloorAttr(boyGObj, 0x2000000) != 0 ||
            (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x2000000) != 0)) {
            i++;
        } else {
            i = 0;
        }
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JPNKORCN(122, 118), 1, 0);

    soundSeDefPlay(1220, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(122, 118), 45, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (scpTriggerFloorAttr(scpSearchGobj(773), 0x2000000) != 0 ||
        scpTriggerFloorAttr(scpSearchGobj(774), 0x2000000) != 0 ||
        scpTriggerFloorAttr(boyGObj, 0x2000000) != 0 ||
        (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x2000000) != 0)) {
        gflagOn(59);
        FinishHint(7);
    }

    switchRChk_mes[0].func = actSt18aSwitchRUpChk;
    sub->mail = switchRChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt18aSwitchRUpChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttrTargetMan(self, 0x2000000) != 0) {
        _ACTWait(1);
    }

    _ACTWait(15);
    gflagOff(PAL_NTSCAUG6JUN26(59, 55));

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(122, 118, 102, 101), 1, 46);
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1220, 1196), 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6JUN26(122, 118, 102, 101), 50, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupItemWithBoundary(1451.0f, 1530.0f, -3039.0f, 100.0f);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6JUN26(122, 118, 102, 101), 90, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    switchRUpChk_mes[0].func = actSt18aSwitchRChk;
    sub->mail = switchRUpChk_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

/* .sbss: the demo's own end flag, raised by the subthread the wait loop below
   spins for, and the flag actSt18aDoorChkSub raises when the door check is
   done. */
static int demoEnd;

static int doorChkDone; /* derived name */

static void actSt18aDoorChk_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    static ActMail doorChk_mes[2] = {{397}, {396}}; /* derived name */

    while (gflagChk(54) == 0 || gflagChk(55) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    gflagOn(56);

    _ACTWait(60);

    stage_SetAnimation(103, 1, 0);

    soundSeDefPlay(1197, 0, 0, 1);
    _ACTWait(50);
    soundSeDefPlay(1198, 0, 0, 1);
    while (stage_CheckAnimationFinish(103) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();
    lt_switch_layout(50);

    doorChk_mes[0].func = actSt18aDoorDownChk;
    sub->mail = doorChk_mes;
    scpBoyControlReadDisable = 0;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static void actSt18aDoorChk_aug6jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    static ActMail doorChk_mes[2] = {{393}, {392}}; /* derived name */

    while (gflagChk(54) == 0 || gflagChk(55) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    gflagOn(56);

    _ACTWait(60);

    stage_SetAnimation(102, 1, 0);

    soundSeDefPlay(1197, 0, 0, 1);
    _ACTWait(60);
    soundSeDefPlay(1198, 0, 0, 1);
    while (stage_CheckAnimationFinish(102) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpWakeupEnemyAll();
    lt_switch_layout(50);

    doorChk_mes[0].func = actSt18aDoorDownChk;
    sub->mail = doorChk_mes;
    scpBoyControlReadDisable = 0;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt18aDoorChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt18aDoorChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt18aDoorChk_aug6jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);
    GProc *th;

    while (gflagChk(58) == 0 || gflagChk(59) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    gflagOn(60);

    th = actCreateSubThread(actSt18aDoorChkSub, 21);
    demoEnd = 0;
    doorChkDone = 0;

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

        stage_SetAnimation(PAL_JPNKORCN(123, 119), 0, -1);

        if (doorChkDone == 0) {
            soundSeDefPlay(1222, 0, 0, 1);
        }

        scpFadeIn(3.0f);
    }

    scpWakeupEnemyAll();
    lt_switch_layout(PAL_JPNKORCN(54, 51));

    doorChk_mes[0].func = actSt18aDoorDownChk;
    sub->mail = doorChk_mes;
    scpBoyControlReadDisable = 0;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt18aDoorDownChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (gflagChk(PAL_NTSCAUG6JUN26(58, 54)) != 0 && gflagChk(PAL_NTSCAUG6JUN26(59, 55)) != 0) {
        _ACTWait(1);
    }

    _ACTWait(15);
    gflagOff(PAL_NTSCAUG6JUN26(60, 56));

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(124, 120, 104, 103), 1, 0);

    soundSeDefPlay(PAL_NTSCAUG6JUN26(1221, 1197), 0, 0, 1);
    _ACTWait(PAL_AUG6JUN26(50, 60));
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1222, 1198), 0, 0, 1);
    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(124, 120, 104, 103)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    doorDownChk_mes[0].func = actSt18aDoorChk;
    sub->mail = doorDownChk_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

void actSt18aEnemy1_1(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    if (ICO_NTSC) {
        Generator_Mask(scpSearchGobj(715));
    } else if (ICO_AUG6JUN26) {
        Generator_Mask(scpSearchGobj(714));
    } else {
        Generator_Mask(scpSearchGobj(765));
        Generator_Mask(scpSearchGobj(766));
    }

    while (gflagChk(PAL_NTSCAUG6JUN26(62, 58)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(30);
    Generator_Call(self);
    _ACTWait(30);
    Generator_Call(self);
    _ACTWait(30);
    Generator_Call(self);
    if (ICO_NTSC) {
        Generator_Call(scpSearchGobj(715));
    } else if (ICO_AUG6JUN26) {
        Generator_Call(scpSearchGobj(714));
    } else {
        Generator_Call(scpSearchGobj(765));
        Generator_Call(scpSearchGobj(766));
    }
}

void actSt18aEnemy2_1(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    Generator_Mask(scpSearchGobj(PAL_NTSC_AUG6JUN26(766, 716, 715)));

    while (gflagChk(PAL_NTSCAUG6JUN26(64, 60)) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_Call(scpSearchGobj(PAL_NTSC_AUG6JUN26(766, 716, 715)));
}

static inline void actSt18aIntro_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(51) == 0) {
        intro_mes[0].func = actSt18aIntroChk;
        act->mail = intro_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt18aIntro_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(51) == 0) {
        intro_mes[0].func = actSt18aIntroChk;
        act->mail = intro_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt18aIntro(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt18aIntro_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt18aIntro_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(55) == 0) {
        intro_mes[0].func = actSt18aIntroChk;
        act->mail = intro_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static inline void actSt18aDoor_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(56) == 0) {
        stage_SetAnimation(103, 0, 0);

        door_mes[0].func = actSt18aDoorChk;
        act->mail = door_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        stage_SetAnimation(103, 0, -1);

        doorDown_mes[0].func = actSt18aDoorDownChk;
        act->mail = doorDown_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt18aDoor_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(56) == 0) {
        stage_SetAnimation(102, 0, 0);

        door_mes[0].func = actSt18aDoorChk;
        act->mail = door_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    } else {
        stage_SetAnimation(102, 0, -1);

        doorDown_mes[0].func = actSt18aDoorDownChk;
        act->mail = doorDown_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt18aDoor(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt18aDoor_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt18aDoor_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(60) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(123, 119), 0, 0);

        door_mes[0].func = actSt18aDoorChk;
        act->mail = door_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(123, 119), 0, -1);

        doorDown_mes[0].func = actSt18aDoorDownChk;
        act->mail = doorDown_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static inline void actSt18aSwitchL_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(54) == 0) {
        stage_SetAnimation(101, 0, 0);

        switchL_mes[0].func = actSt18aSwitchLChk;
        act->mail = switchL_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        stage_SetAnimation(101, 0, 45);

        switchLUp_mes[0].func = actSt18aSwitchLUpChk;
        act->mail = switchLUp_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt18aSwitchL_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(54) == 0) {
        stage_SetAnimation(100, 0, 0);

        switchL_mes[0].func = actSt18aSwitchLChk;
        act->mail = switchL_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    } else {
        stage_SetAnimation(100, 0, 45);

        switchLUp_mes[0].func = actSt18aSwitchLUpChk;
        act->mail = switchLUp_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt18aSwitchL(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt18aSwitchL_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt18aSwitchL_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(58) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(121, 117), 0, 0);

        switchL_mes[0].func = actSt18aSwitchLChk;
        act->mail = switchL_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(121, 117), 0, 45);

        switchLUp_mes[0].func = actSt18aSwitchLUpChk;
        act->mail = switchLUp_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static inline void actSt18aSwitchR_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(55) == 0) {
        stage_SetAnimation(102, 0, 0);

        switchR_mes[0].func = actSt18aSwitchRChk;
        act->mail = switchR_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        stage_SetAnimation(102, 0, 45);

        switchRUp_mes[0].func = actSt18aSwitchRUpChk;
        act->mail = switchRUp_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt18aSwitchR_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(55) == 0) {
        stage_SetAnimation(101, 0, 0);

        switchR_mes[0].func = actSt18aSwitchRChk;
        act->mail = switchR_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    } else {
        stage_SetAnimation(101, 0, 45);

        switchRUp_mes[0].func = actSt18aSwitchRUpChk;
        act->mail = switchRUp_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt18aSwitchR(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt18aSwitchR_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt18aSwitchR_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(59) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(122, 118), 0, 0);

        switchR_mes[0].func = actSt18aSwitchRChk;
        act->mail = switchR_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(122, 118), 0, 45);

        switchRUp_mes[0].func = actSt18aSwitchRUpChk;
        act->mail = switchRUp_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static inline void actSt18aEne_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(57) == 0) {
        ene_mes[0].func = actSt18aEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt18aEne_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(57) == 0) {
        ene_mes[0].func = actSt18aEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt18aEne(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt18aEne_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt18aEne_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(61) == 0) {
        ene_mes[0].func = actSt18aEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static inline void actSt18aEnemy1_2_ntscaug6jun26(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(58) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(30);
    Generator_Call(self);
    _ACTWait(30);
    Generator_Call(self);
    _ACTWait(30);
    Generator_Call(self);
}

void actSt18aEnemy1_2(GObj *volatile self)
{
    if (ICO_NTSCAUG6JUN26) {
        actSt18aEnemy1_2_ntscaug6jun26(self);
        return;
    }
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(62) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(30);
    Generator_Call(self);
    _ACTWait(30);
    Generator_Call(self);
    _ACTWait(30);
    Generator_Call(self);
}

static inline void actSt18aEne2_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(59) == 0) {
        ene2_mes[0].func = actSt18aEne2Chk;
        act->mail = ene2_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt18aEne2_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(59) == 0) {
        ene2_mes[0].func = actSt18aEne2Chk;
        act->mail = ene2_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt18aEne2(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt18aEne2_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt18aEne2_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(63) == 0) {
        ene2_mes[0].func = actSt18aEne2Chk;
        act->mail = ene2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static inline void actSt18aEnemy2_2_ntscaug6jun26(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(60) == 0) {
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

void actSt18aEnemy2_2(GObj *volatile self)
{
    if (ICO_NTSCAUG6JUN26) {
        actSt18aEnemy2_2_ntscaug6jun26(self);
        return;
    }
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(64) == 0) {
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

void actSt18aCamera(int x)
{
    volatile int local = x;
}

static void actSt18aDoorChkSub(GObj *volatile self)
{
    _ACTWait(60);

    stage_SetAnimation(PAL_JPNKORCN(123, 119), 1, 0);

    if (scpTriggerFloorAttr(boyGObj, 0x5000000) != 0) {
        SetCameraFlag_LwsCutBack();
    }

    soundSeDefPlay(1221, 0, 0, 1);
    _ACTWait(50);
    doorChkDone = 1;
    soundSeDefPlay(1222, 0, 0, 1);
    while (stage_CheckAnimationFinish(PAL_JPNKORCN(123, 119)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

static inline void actSt18aEneChk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    gflagOff(358);
    gflagOn(57);
    gflagOn(58);
}

static inline void actSt18aEneChk_aug6jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    gflagOff(357);
    gflagOn(57);
    gflagOn(58);
}

void actSt18aEneChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt18aEneChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt18aEneChk_aug6jun26(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    gflagOff(391);
    gflagOn(61);
    gflagOn(62);
}

static inline void actSt18aEne2Chk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (girlGObj == 0 || scpTriggerFloorAttr(scpSearchGobj(724), 0x4000000) == 0) {
        _ACTWait(1);
    }
    _ACTWait(300);
    gflagOn(59);
    gflagOn(60);
}

static inline void actSt18aEne2Chk_aug6jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (girlGObj == 0 || scpTriggerFloorAttr(scpSearchGobj(723), 0x4000000) == 0) {
        _ACTWait(1);
    }
    _ACTWait(600);
    gflagOn(59);
    gflagOn(60);
}

void actSt18aEne2Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt18aEne2Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt18aEne2Chk_aug6jun26(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }
    while (girlGObj == 0 || scpTriggerFloorAttr(scpSearchGobj(774), 0x4000000) == 0) {
        _ACTWait(1);
    }
    FinishHint(8);
    _ACTWait(300);
    gflagOn(63);
    gflagOn(64);
}
