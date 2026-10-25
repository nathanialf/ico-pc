#include "st08a.h"
#include "debug.h"
#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "act.h"
#include "gobj_process.h"
#include "commonact.h"
#include "way_llf.h"
#include "camera-root.h"
#include "generator.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "Shadow.h"
#include "StageAnimation.h"
#include "typedef.h"
#include "main.h"
#include "script.h"

/* GObj.drawMask (0x50): cleared so no camera draws the object */
#define SCP_CLEAR_DRAWMASK(id) (scpSearchGobj(id)->drawMask = 0)
#define SCP_SET_DRAWMASK(id) (scpSearchGobj(id)->drawMask = 0xFFFFFFFF)

static void actSt08aDoorUpSub(GObj *volatile self);
static void actSt08aGirlPosChk(GObj *volatile self);
static void actSt08aHasiMain(GObj *volatile self);
static void actSt08aHasiSwitch(GObj *volatile self);
static void actSt08aHint1Chk(GObj *volatile self);
static void actSt08aTorchOffChk(GObj *volatile self);

static ActMail ene1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail ene1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail ene1_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail ene1_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define ene1_mes PAL_NTSC_AUG6_JUN26(ene1_mes_pal, ene1_mes_ntsc, ene1_mes_aug6, ene1_mes_jun26)

static ActMail ene2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail ene2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail ene2_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail ene2_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define ene2_mes PAL_NTSC_AUG6_JUN26(ene2_mes_pal, ene2_mes_ntsc, ene2_mes_aug6, ene2_mes_jun26)

static ActMail door_main_mes_pal[2] = {{407, actSt08aDoorSwitch}, {429}}; /* derived name */

static ActMail door_main_mes_ntsc[2] = {{377, actSt08aDoorSwitch}, {396}}; /* derived name */

static ActMail door_main_mes_aug6[2] = {{373, actSt08aDoorSwitch}, {392}}; /* derived name */

static ActMail door_main_mes_jun26[2] = {{357, actSt08aDoorSwitch}, {376}}; /* derived name */

#define door_main_mes                                                                              \
    PAL_NTSC_AUG6_JUN26(door_main_mes_pal, door_main_mes_ntsc, door_main_mes_aug6,                 \
                        door_main_mes_jun26)

static ActMail door_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail door_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define door_mes PAL_NTSC_AUG6_JUN26(door_mes_pal, door_mes_ntsc, door_mes_aug6, door_mes_jun26)

static ActMail door_switch_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door_switch_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door_switch_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail door_switch_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define door_switch_mes                                                                            \
    PAL_NTSC_AUG6_JUN26(door_switch_mes_pal, door_switch_mes_ntsc, door_switch_mes_aug6,           \
                        door_switch_mes_jun26)

static float door_up_sound_pos[4] = {-3092.0f, -2727.0f, 3711.0f, 0.0f}; /* derived name */

static ActMail intro_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail intro_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail intro_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define intro_mes PAL_NTSC_AUG6JUN26(intro_mes_pal, intro_mes_ntsc, intro_mes_aug6jun26)

static ActMail girl_pos_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hint1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hasi_main_mes[2] = {{408, actSt08aHasiSwitch}, {429}}; /* derived name */

static ActMail hasi_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hasi_switch_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch_on_mes[2] = {{430}, {429}}; /* derived name */

static ActMail torch_off_mes[2] = {{430}, {429}}; /* derived name */

/* .sbss: the demo's own end flag, raised by the subthreads the wait loops
   below spin for, and the flag actSt08aDoorUpSub raises when the door is all
   the way up. */
static int demoEnd;

static int doorUpDone; /* derived name */

void actSt08aEnd(void)
{
    if (girlGObj != 0) {
        if (gflagChk(PAL_NTSCAUG6JUN26(73, 69)) == 0) {
            gflagOn(PAL_NTSC_AUG6JUN26(391, 358, 357));
        }
    }
}

static void actSt08aEne1Chk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while ((scpTriggerFloorAttr(girlGObj, 0x1000000) == 0 ||
            scpTriggerFloorAttr(boyGObj, 0x6000000) == 0) &&
           (scpTriggerFloorAttr(girlGObj, 0x1000000) &
            scpTriggerFloorAttr(scpSearchGobj(331), 0x1000000)) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyOne(3453);
    gflagOn(68);
    gflagOn(70);
    stage_SetAnimation(89, 1, 0);

    while (stage_CheckAnimationFinish(89) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    scpWakeupEnemyOne(3453);
}

static void actSt08aEne1Chk_jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while ((scpTriggerFloorAttr(girlGObj, 0x1000000) &
            scpTriggerFloorAttr(scpSearchGobj(322), 0x1000000)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(30);
    gflagOn(67);
    gflagOn(69);
}

static void actSt08aEne1Chk_aug6(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while ((scpTriggerFloorAttr(girlGObj, 0x1000000) == 0 ||
            scpTriggerFloorAttr(boyGObj, 0x6000000) == 0) &&
           (scpTriggerFloorAttr(girlGObj, 0x1000000) &
            scpTriggerFloorAttr(scpSearchGobj(330), 0x1000000)) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyOne(3415);
    gflagOn(68);
    gflagOn(70);
    stage_SetAnimation(88, 1, 0);

    while (stage_CheckAnimationFinish(88) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    scpWakeupEnemyOne(3415);
}

void actSt08aEne1Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt08aEne1Chk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt08aEne1Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt08aEne1Chk_aug6(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (gflagChk(71) == 0 || (scpTriggerFloorAttr(boyGObj, 0x6000000) == 0 &&
                                 scpTriggerFloorAttr(girlGObj, 0x1000000) == 0)) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyOne(PAL_JPNKORCN(3757, 3734));
    gflagOn(72);
    gflagOn(74);
    stage_SetAnimation(PAL_JPNKORCN(103, 99), 1, 0);

    if (scpTriggerFloorAttr(boyGObj, 0x6000000) == 0 &&
        scpTriggerFloorAttr(boyGObj, 0x1000000) == 0) {
        SetCameraFlag_LwsCutBack();
    }

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(103, 99)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpWakeupEnemyOne(PAL_JPNKORCN(3757, 3734));
}

static void actSt08aEne2Chk_ntsc(GObj *volatile self)
{
    int save;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(girlGObj, 0x5000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyOne(3453);
    gflagOff(358);
    gflagOn(69);
    gflagOn(71);
    _ACTWait(60);

    save = iosPadActRequestEnable;
    iosPadActRequestEnable = 0;

    scpKillEnemyOne(304);
    scpKillEnemyOne(305);
    scpKillEnemyOne(306);
    scpKillSpiderGroup(307);

    stage_SetAnimation(90, 1, 0);

    while (stage_CheckAnimationFinish(90) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequestEnable = save;
    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyOne(3453);
}

static void actSt08aEne2Chk_jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(girlGObj, 0x5000000) == 0) {
        _ACTWait(1);
    }

    gflagOn(68);
    gflagOn(70);
    _ACTWait(60);

    stage_SetAnimation(80, 1, 0);

    while (stage_CheckAnimationFinish(80) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

static void actSt08aEne2Chk_aug6(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(girlGObj, 0x5000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyOne(3415);
    gflagOff(357);
    gflagOn(69);
    gflagOn(71);
    _ACTWait(60);

    scpKillEnemyOne(303);
    scpKillEnemyOne(304);
    scpKillEnemyOne(305);
    scpKillSpiderGroup(306);

    stage_SetAnimation(89, 1, 0);

    while (stage_CheckAnimationFinish(89) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyOne(3415);
}

void actSt08aEne2Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt08aEne2Chk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt08aEne2Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt08aEne2Chk_aug6(self);
        return;
    }
    int save;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (gflagChk(72) == 0 || scpTriggerFloorAttr(girlGObj, 0x5000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyOne(PAL_JPNKORCN(3757, 3734));
    gflagOff(391);
    gflagOn(73);
    gflagOn(75);
    _ACTWait(60);

    save = iosPadActRequestEnable;
    iosPadActRequestEnable = 0;

    scpKillEnemyOne(334);
    scpKillEnemyOne(335);
    scpKillEnemyOne(336);
    scpKillSpiderGroup(337);
    Generator_Delete(scpSearchGobj(338));
    Generator_Delete(scpSearchGobj(339));

    stage_SetAnimation(PAL_JPNKORCN(104, 100), 1, 0);

    if (scpTriggerFloorAttr(boyGObj, 0x5000000) == 0) {
        SetCameraFlag_LwsCutBack();
    }

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(104, 100)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequestEnable = save;
    scpPlayMot(boyGObj, 0);
    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyOne(PAL_JPNKORCN(3757, 3734));
}

static void actSt08aDoorUp_ntsc(GObj *volatile self)
{
    lt_switch_layout(51);
    gflagOn(67);
    scpSleepEnemyAll();

    _ACTWait(60);

    stage_SetAnimation(91, 1, 0);

    soundSeDefPlay(1197, 0, door_up_sound_pos, 1);
    _ACTWait(30);
    soundSeDefPlay(1198, 0, door_up_sound_pos, 1);

    while (stage_CheckAnimationFinish(91) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    SetWayGroupActive(11, 1);
    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static void actSt08aDoorUp_aug6jun26(GObj *volatile self)
{
    lt_switch_layout(51);
    if (ICO_JUN26) {
        gflagOn(66);
    } else {
        gflagOn(67);
        scpSleepEnemyAll();
    }

    _ACTWait(60);

    stage_SetAnimation(PAL_JUN26(90, 81), 1, 0);

    soundSeDefPlay(PAL_JUN26(1197, 1171), 0, door_up_sound_pos, 1);
    _ACTWait(30);
    soundSeDefPlay(PAL_JUN26(1198, 1172), 0, door_up_sound_pos, 1);

    while (stage_CheckAnimationFinish(PAL_JUN26(90, 81)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    SetWayGroupActive(11, 1);
    if (!ICO_JUN26) {
        scpWakeupEnemyAll();
    }
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actSt08aDoorUp(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt08aDoorUp_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt08aDoorUp_aug6jun26(self);
        return;
    }
    GProc *th;

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    gflagOn(71);
    scpSleepEnemyAll();

    th = actCreateSubThread(actSt08aDoorUpSub, 21);
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
        stage_SetAnimation(PAL_JPNKORCN(105, 101), 0, -1);
        scpFadeIn(3.0f);
        if (doorUpDone == 0) {
            soundSeDefPlay(1222, 0, 0, 1);
        }
    }

    SetWayGroupActive(11, 1);
    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

/* .sdata: the scene's stream handle. */
static SqEntry *st08a_adpcm = 0; /* derived name */

static void actSt08aHasiUpSub(GObj *volatile self)
{
    _ACTWait(60);

    while (st08a_adpcm == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JPNKORCN(106, 102), 1, 0);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(106, 102), 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    AdpcmPlay(st08a_adpcm->stream);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(106, 102), 180, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActRequest(boyPad, 17);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(106, 102)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

static void actSt08aHasiUp(GObj *volatile self)
{
    GProc *th;

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    gflagOn(79);
    SetWayGroupActive(30, 1);
    scpSleepEnemyAll();

    demoEnd = 0;
    scpAdpcmPlayRequestFunc(98, &st08a_adpcm, 1, 1, 0);

    th = actCreateSubThread(actSt08aHasiUpSub, 21);

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&th->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);
        while (st08a_adpcm == 0) {
            _ACTWait(1);
        }
        scpAdpcmFadeCloseFunc(&st08a_adpcm, 512);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }
        stage_SetAnimation(PAL_JPNKORCN(106, 102), 0, -1);
        scpFadeIn(3.0f);
    }

    scpWakeupEnemyAll();
    scpSearchGobj(369)->active = 0;
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

static void actSt08aTorchOnChk(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0x5000000) != 0 ||
           scpTriggerFloorAttr(boyGObj, 0x7000000) != 0 ||
           scpTriggerFloorAttr(boyGObj, 0xA000000) != 0) {
        _ACTWait(1);
    }

    SCP_SET_DRAWMASK(350);
    SCP_SET_DRAWMASK(351);
    SCP_SET_DRAWMASK(352);
    SCP_SET_DRAWMASK(353);
    SCP_SET_DRAWMASK(354);
    SCP_SET_DRAWMASK(355);
    SCP_SET_DRAWMASK(356);
    SCP_SET_DRAWMASK(357);
    SCP_SET_DRAWMASK(358);
    SCP_SET_DRAWMASK(359);
    SCP_SET_DRAWMASK(348);

    torch_on_mes[0].func = actSt08aTorchOffChk;
    act->mail = torch_on_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt08aTorchOffChk(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0x5000000) == 0 &&
           scpTriggerFloorAttr(boyGObj, 0x7000000) == 0 &&
           scpTriggerFloorAttr(boyGObj, 0xA000000) == 0) {
        _ACTWait(1);
    }

    SCP_CLEAR_DRAWMASK(350);
    SCP_CLEAR_DRAWMASK(351);
    SCP_CLEAR_DRAWMASK(352);
    SCP_CLEAR_DRAWMASK(353);
    SCP_CLEAR_DRAWMASK(354);
    SCP_CLEAR_DRAWMASK(355);
    SCP_CLEAR_DRAWMASK(356);
    SCP_CLEAR_DRAWMASK(357);
    SCP_CLEAR_DRAWMASK(358);
    SCP_CLEAR_DRAWMASK(359);
    SCP_CLEAR_DRAWMASK(348);

    torch_off_mes[0].func = actSt08aTorchOnChk;
    act->mail = torch_off_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt08aDoor(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(71, 67, 66)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(105, 101, 91, 90, 81), 0, 0);
        door_mes[0].func = actSt08aDoorMain;
        act->mail = door_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        SetWayGroupActive(11, 1);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(105, 101, 91, 90, 81), 0, -1);
    }
}

static void actSt08aEne1_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(68) == 0) {
        ene1_mes[0].func = actSt08aEne1Chk;
        act->mail = ene1_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actSt08aEne1_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_JUN26(68, 67)) == 0) {
        ene1_mes[0].func = actSt08aEne1Chk;
        act->mail = ene1_mes;
        ACTSendMailCorrect(self, PAL_JUN26(393, 377));
        _ACTWait(0);
    }
}

void actSt08aEne1(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt08aEne1_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt08aEne1_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    shadow_SetLength(scpSearchGobj(366)->dobj, 100.0f);
    shadow_SetLength(scpSearchGobj(367)->dobj, 100.0f);
    shadow_SetLength(scpSearchGobj(368)->dobj, 100.0f);

    if (gflagChk(72) == 0) {
        ene1_mes[0].func = actSt08aEne1Chk;
        act->mail = ene1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt08aEne2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(73, 69, 68)) == 0) {
        ene2_mes[0].func = actSt08aEne2Chk;
        act->mail = ene2_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }
}

void actSt08aEnemy1(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6_JUN26(74, 70, 69)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(116);

    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt08aEnemy2(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6_JUN26(74, 70, 69)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(100);

    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt08aEnemy3(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6_JUN26(75, 71, 70)) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt08aEnemy4(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6_JUN26(75, 71, 70)) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt08aIntro(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(76, 72)) == 0) {
        intro_mes[0].func = actSt08aIntroChk;
        act->mail = intro_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt08aHint1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(77) == 0) {
        hint1_mes[0].func = actSt08aHint1Chk;
        act->mail = hint1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        FinishHint(2);
    }
}

void actSt08aGirlPos(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(78) == 0) {
        SleepHint(2);
        girl_pos_mes[0].func = actSt08aGirlPosChk;
        act->mail = girl_pos_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt08aHasi(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(79) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(106, 102), 0, 0);
        hasi_mes[0].func = actSt08aHasiMain;
        act->mail = hasi_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(106, 102), 0, -1);
        scpSearchGobj(369)->active = 0;
        SetWayGroupActive(30, 1);
    }
}

void actSt08aTorch(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    torch_mes[0].func = actSt08aTorchOffChk;
    act->mail = torch_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt08aInit(void)
{
    float f = 0.95f;

    scpSetCageVelocityFriction(PAL_NTSC_AUG6_JUN26(365, 336, 335, 327), f);
    scpSetCageVelocityFriction(PAL_NTSC_AUG6_JUN26(364, 335, 334, 326), f);

    if (gflagChk(PAL_NTSCAUG6_JUN26(80, 73, 71)) != 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(370, 366, 335, 334, 301), 0, 510);
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(365, 336, 335, 327))->active = 0;
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(370, 366, 335, 334, 301), 0, 0);
        scpSearchGobj(PAL_NTSC_AUG6_JUN26(364, 335, 334, 326))->active = 0;
    }
}

void actSt08aDoorMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 0;

    sub->mainMail = door_main_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt08aDoorSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    scpBoyControlReadDisable = 1;

    door_switch_mes[0].func = actSt08aDoorUp;
    sub->mail = door_switch_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

static void actSt08aDoorUpSub(GObj *volatile self)
{
    _ACTWait(60);

    stage_SetAnimation(PAL_JPNKORCN(105, 101), 1, 0);

    soundSeDefPlay(1221, 0, door_up_sound_pos, 1);
    _ACTWait(30);
    doorUpDone = 1;
    soundSeDefPlay(1222, 0, door_up_sound_pos, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(105, 101)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

void actSt08aIntroChk(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 100.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
    scpBoyControlReadDisable = 1;

    gflagOn(PAL_NTSCAUG6JUN26(76, 72));

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(135, 131, 115, 114), 1, 0);
    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(135, 131, 115, 114)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));
    scpBoyControlReadDisable = 0;
}

static void actSt08aGirlPosChk(GObj *volatile self)
{
    while (girlGObj == 0 || scpTriggerBall(self, girlGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    gflagOn(78);
    WakeupHint(2);
}

static void actSt08aHint1Chk(GObj *volatile self)
{
    while (gflagChk(80) == 0) {
        _ACTWait(1);
    }

    debug_StdPrintfDummy("HINT1_FINISH!!!!!!!!!!!!!!!\n");

    gflagOn(77);
    FinishHint(2);
}

static void actSt08aHasiMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 0;

    sub->mainMail = hasi_main_mes;
    while (1) {
        _ACTWait(1);
    }
}

static void actSt08aHasiSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    scpBoyControlReadDisable = 1;

    hasi_switch_mes[0].func = actSt08aHasiUp;
    sub->mail = hasi_switch_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}
