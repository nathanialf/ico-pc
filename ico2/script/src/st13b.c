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

/* The functions stand in the order of the stage's source (aug6's TRFILE
   numbers them so). The ones defined inline are output at the end of the
   file, in the order of the declarations below, which gives this build's
   .text order; the others are output where they stand. */
inline void actSt13bSekizo(GObj *volatile self);
inline void actSt13bSekizo2(GObj *volatile self);
inline void actSt13bBossAfter(GObj *volatile self);
inline void actSt13bStoneGirl(GObj *volatile self);
inline void actSt13bExit(GObj *volatile self);
inline void actSt13bElev(GObj *volatile self);
inline void actSt13bDoor(GObj *volatile self);
inline void actBossTest(GObj *volatile self);
inline void actSt13bConte02Jimaku(GObj *volatile self);
inline void actSt13bSekizo2Event(int x);
inline void actSt13bMeetAgainChk(GObj *volatile self);
inline void actConte10a(GObj *volatile self);
inline void actSt13bBossChk(GObj *volatile self);
inline void actSt13bBossAfterChk(GObj *volatile self);
inline void actConte10c(GObj *volatile self);
inline void actSt13bExitChk(GObj *volatile self);
inline void actSt13bElevMain(GObj *volatile self);
inline void actSt13bElevSwitch(GObj *volatile self);
inline void actSt13bDoorMain(GObj *volatile self);
inline void actSt13bDoorSwitch(GObj *volatile self);
inline void actSt13bDoorUp(GObj *volatile self);

#include "ico_gamestate.h" /* port: achievement signals */

static void actSt13bDoorUpSub(GObj *volatile self);
static void actSt13bElev2CharaChk(GObj *volatile self);
static void actSt13bElevUpSub(GObj *volatile self);

/* .sbss: the demo's own end flag, the conte-02 end flag, the pad actuator
   handle and volume byte the door-up subthread holds, and the flag
   actSt13bDoorUpSub raises when the door is up. */
static int demoEnd_pal;

static int demoEnd_aug6jun26; /* derived name */

#define demoEnd (*PAL_AUG6JUN26(&demoEnd_pal, &demoEnd_aug6jun26))

static int conte02End; /* derived name */

static int padAct; /* derived name */

static unsigned char padActVolume; /* derived name */

static int doorUpDone; /* derived name */

/* .data: actor mail packets. */
static ActMail floor_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail floor_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail floor_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail floor_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define floor_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(floor_mes_pal, floor_mes_ntsc, floor_mes_aug6, floor_mes_jun26)

static ActMail sekizo_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail sekizo_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail sekizo_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail sekizo_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define sekizo_mes                                                                                 \
    PAL_NTSC_AUG6_JUN26(sekizo_mes_pal, sekizo_mes_ntsc, sekizo_mes_aug6, sekizo_mes_jun26)

static ActMail sekizo2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail sekizo2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail sekizo2_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail sekizo2_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define sekizo2_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(sekizo2_mes_pal, sekizo2_mes_ntsc, sekizo2_mes_aug6, sekizo2_mes_jun26)

static ActMail meetAgain_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail meetAgain_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail meetAgain_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail meetAgain_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define meetAgain_mes                                                                              \
    PAL_NTSC_AUG6_JUN26(meetAgain_mes_pal, meetAgain_mes_ntsc, meetAgain_mes_aug6,                 \
                        meetAgain_mes_jun26)

static ActMail boss_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail boss_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail boss_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail boss_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define boss_mes PAL_NTSC_AUG6_JUN26(boss_mes_pal, boss_mes_ntsc, boss_mes_aug6, boss_mes_jun26)

static ActMail bossAfter_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail bossAfter_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail bossAfter_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail bossAfter_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define bossAfter_mes                                                                              \
    PAL_NTSC_AUG6_JUN26(bossAfter_mes_pal, bossAfter_mes_ntsc, bossAfter_mes_aug6,                 \
                        bossAfter_mes_jun26)

static ActMail exit_mes_pal[2] = {{393}, {392}}; /* derived name */

static ActMail exit_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define exit_mes PAL_JUN26(exit_mes_pal, exit_mes_jun26)

static ActMail elevMain_mes_pal[2] = {{406, actSt13bElevSwitch}, {429}}; /* derived name */

static ActMail elevMain_mes_ntsc[2] = {{376, actSt13bElevSwitch}, {396}}; /* derived name */

static ActMail elevMain_mes_aug6[2] = {{372, actSt13bElevSwitch}, {392}}; /* derived name */

static ActMail elevMain_mes_jun26[2] = {{356, actSt13bElevSwitch}, {376}}; /* derived name */

#define elevMain_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(elevMain_mes_pal, elevMain_mes_ntsc, elevMain_mes_aug6, elevMain_mes_jun26)

static ActMail elev_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail elev_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail elev_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail elev_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define elev_mes PAL_NTSC_AUG6_JUN26(elev_mes_pal, elev_mes_ntsc, elev_mes_aug6, elev_mes_jun26)

static ActMail elevSwitch_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail elevSwitch_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail elevSwitch_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail elevSwitch_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define elevSwitch_mes                                                                             \
    PAL_NTSC_AUG6_JUN26(elevSwitch_mes_pal, elevSwitch_mes_ntsc, elevSwitch_mes_aug6,              \
                        elevSwitch_mes_jun26)

static ActMail elevUp_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail elevUp_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail elevUp_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail elevUp_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define elevUp_mes                                                                                 \
    PAL_NTSC_AUG6_JUN26(elevUp_mes_pal, elevUp_mes_ntsc, elevUp_mes_aug6, elevUp_mes_jun26)

static ActMail doorMain_mes_pal[2] = {{407, actSt13bDoorSwitch}, {429}}; /* derived name */

static ActMail doorMain_mes_ntsc[2] = {{377, actSt13bDoorSwitch}, {396}}; /* derived name */

static ActMail doorMain_mes_aug6[2] = {{373, actSt13bDoorSwitch}, {392}}; /* derived name */

static ActMail doorMain_mes_jun26[2] = {{357, actSt13bDoorSwitch}, {376}}; /* derived name */

#define doorMain_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(doorMain_mes_pal, doorMain_mes_ntsc, doorMain_mes_aug6, doorMain_mes_jun26)

static ActMail door_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail door_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail door_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail door_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define door_mes PAL_NTSC_AUG6_JUN26(door_mes_pal, door_mes_ntsc, door_mes_aug6, door_mes_jun26)

static ActMail doorSwitch_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail doorSwitch_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail doorSwitch_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail doorSwitch_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define doorSwitch_mes                                                                             \
    PAL_NTSC_AUG6_JUN26(doorSwitch_mes_pal, doorSwitch_mes_ntsc, doorSwitch_mes_aug6,              \
                        doorSwitch_mes_jun26)

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

/* the 2001 builds keep the pad-shake handle and its volume in these two (sekizo_13b and
   sekizo_13b_vol there, stream handles in the European program) */
int sekizo_13b_ntscaug6jun26 = 0;

unsigned char sekizo_13b_vol_ntscaug6jun26 = 0;

int st13b_yure = 0;

unsigned char st13b_yure_vol = 0;

static int st13b_boss_yure = 0; /* derived name */

static unsigned char st13b_boss_yure_vol = 0; /* derived name */

static void actSt13bFloor_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(5) == 0) {
        scpFadeOut(255.0f, 0, 0, 0);
        lt_switch_layout(51);

        stage_SetAnimation(25, 0, 0);
        stage_SetAnimation(21, 0, 0);
        stage_SetAnimation(28, 0, 0);
        stage_SetAnimation(29, 0, 0);
        stage_SetAnimation(31, 0, 0);
        stage_SetAnimation(33, 0, 0);
        stage_SetAnimation(35, 0, 235);
        stage_SetAnimation(36, 0, 0);

        floor_mes[0].func = actSt13bFloorChk;
        act->mail = floor_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    } else {
        stage_SetAnimation(27, 0, -1);
        stage_SetAnimation(21, 0, -1);
        stage_SetAnimation(31, 0, 0);
        stage_SetAnimation(33, 0, 0);
    }
}

void actSt13bFloor(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt13bFloor_jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(5) == 0) {
        scpFadeOut(255.0f, 0, 0, 0);
        if (ICO_NTSCAUG6JUN26) {
            lt_switch_layout(51);
            scpBoyControlReadDisable = 1;

            stage_SetAnimation(29, 0, 0);
            stage_SetAnimation(25, 0, 0);
            stage_SetAnimation(32, 0, 0);
            stage_SetAnimation(33, 0, 0);
            stage_SetAnimation(35, 0, 0);
            stage_SetAnimation(37, 0, 0);
            stage_SetAnimation(39, 0, 235);
            stage_SetAnimation(40, 0, 0);
        } else if (ICO_JPNKORCN) {
            lt_switch_layout(52);
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
        } else {
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
        }

        floor_mes[0].func = actSt13bFloorChk;
        act->mail = floor_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_NTSCAUG6JUN26(32, 31), 0, -1);
        stage_SetAnimation(PAL_NTSCAUG6JUN26(26, 25), 0, -1);
        stage_SetAnimation(PAL_NTSCAUG6JUN26(36, 35), 0, 0);
        stage_SetAnimation(PAL_NTSCAUG6JUN26(38, 37), 0, 0);
    }
}

void actSt13bFloorChk(GObj *volatile self)
{
    int v = 32;

    gflagOn(5);
    actCreateSubThread(actSt13bConte02, 21);
    actCreateSubThread(actSt13bConte02Jimaku, 21);

    if (ICO_AUG6JUN26) {
        demoEnd = 0;
    } else {
        conte02End = 0;
    }
    st13b_adpcm = 0;

    do {
        _ACTWait(1);
    } while (st13b_adpcm == 0);

    while (PAL_AUG6JUN26(conte02End, demoEnd) == 0 &&
           ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }
    ico_gs_signal(ICO_GS_EV_DEMO_END, 8 + (conte02End == 0));

    if (PAL_AUG6JUN26(conte02End, demoEnd) == 0) {
        v = 64;
    }

    if (st13b_adpcm != 0) {
        if (ICO_NTSCAUG6JUN26) {
            scpAdpcmFadeClose(st13b_adpcm, v);
        } else {
            scpAdpcmFadeCloseFunc(&st13b_adpcm, v);
        }
    }

    RequestStageChange(4, boyGObj, 0, 0.025f, 2.0f);
}

static void actSt13bConte02_ntsc(GObj *volatile self)
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

    stage_SetAnimation(515, 1, 0);
    scpPlayMot((void *)boyGObj, 276);
    scpPlayMot(scpSearchGobj(2197), 940);
    scpSearchGobj(2197)->active = 1;
    scpPlayMot(scpSearchGobj(2198), 962);
    scpSearchGobj(2198)->active = 1;
    scpPlayMot(scpSearchGobj(2199), 986);
    scpSearchGobj(2199)->active = 1;
    _ACTWait(1);

    stage_SetAnimation(33, 1, 0);
    stage_SetAnimation(20, 1, 0);

    _ACTWait(
        ico_d2i(ico_dmul(ico_i2d((60 - systemStatus[0] * 10) / systemStatus[1]), ICO_D(0.15))));

    AdpcmPlay(st13b_adpcm->stream);

    while (stage_ContinueAnimation(515, 516) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 277);
    scpPlayMot(scpSearchGobj(2197), 941);
    scpPlayMot(scpSearchGobj(2198), 963);
    scpPlayMot(scpSearchGobj(2199), 987);
    _ACTWait(1);

    stage_SetAnimation(26, 1, 0);
    stage_SetAnimation(21, 1, 0);

    while (stage_ContinueAnimation(516, 517) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    stage_SetAnimation(27, 1, 0);

    while (stage_ContinueAnimation(517, 518) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 278);
    scpPlayMot(scpSearchGobj(2197), 942);
    scpPlayMot(scpSearchGobj(2198), 964);
    scpPlayMot(scpSearchGobj(2199), 988);
    _ACTWait(1);

    stage_SetAnimation(28, 1, 0);
    stage_SetAnimation(22, 1, 0);

    while (stage_ContinueAnimation(518, 519) == 0) {
        _ACTWait(1);
    }

    scpFadeIn(3.0f);

    scpPlayMot((void *)boyGObj, 279);
    scpPlayMot(scpSearchGobj(2198), 965);
    _ACTWait(1);

    stage_SetAnimation(29, 1, 0);

    while (stage_ContinueAnimation(519, 520) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 280);
    scpPlayMot(scpSearchGobj(2197), 941);
    scpPlayMot(scpSearchGobj(2198), 963);
    scpPlayMot(scpSearchGobj(2199), 987);
    _ACTWait(1);

    stage_SetAnimation(30, 1, 0);

    while (stage_ContinueAnimation(520, 521) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(2197), 943);
    scpPlayMot(scpSearchGobj(2198), 966);
    scpPlayMot(scpSearchGobj(2199), 989);
    _ACTWait(1);

    stage_SetAnimation(23, 1, 0);

    while (stage_ContinueAnimation(521, 522) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 281);
    scpSearchGobj(2197)->active = 0;
    scpSearchGobj(2198)->active = 0;
    scpSearchGobj(2199)->active = 0;

    stage_SetAnimation(33, 0, 0);

    while (stage_CheckAnimationFrame(522, 200, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpFadeOut(3.0f, 0, 0, 0);

    while (scpFadeChk() != 0) {
        _ACTWait(45);
    }

    stage_SetAnimation(522, -1, -2);
    scpFadeIn(3.0f);
    stage_SetAnimation(523, 1, 0);

    while (stage_ContinueAnimation(523, 524) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 282);
    _ACTWait(1);

    stage_SetAnimation(25, 1, 0);
    _ACTWait(60);

    padAct = iosPadActRequest(boyPad, 9);
    padActVolume = 128;
    iosPadActVolumeSet(padAct, 128);

    while (stage_ContinueAnimation(524, 525) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(525, 526) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(24, 1, 0);

    while (stage_ContinueAnimation(526, 527) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 283);
    _ACTWait(130);

    stage_SetAnimation(31, 1, 0);

    while (stage_ContinueAnimation(527, 528) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(528, 529) == 0) {
        _ACTWait(1);
    }

    iosPadActStop(padAct);
    scpPlayMot((void *)boyGObj, 284);

    while (stage_ContinueAnimation(529, 530) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 282);

    while (stage_ContinueAnimation(530, 531) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 286);
    while (stage_ContinueAnimation(531, 532) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 287);
    iosPadActRequest(boyPad, 16);

    while (stage_ContinueAnimation(532, 533) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 288);

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

    while (stage_ContinueAnimation(533, 534) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 289);

    while (stage_ContinueAnimation(534, 535) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 290);

    while (stage_ContinueAnimation(535, 536) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 291);

    while (stage_CheckAnimationFrame(536, 150, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    conte02End = 1;
}

static void actSt13bConte02_jun26(GObj *volatile self)
{
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    stgmgrNextStagePreLoadForceStageSet(0);
    scpAdpcmPlayRequestFunc(14, &st13b_adpcm, 0, 1, 0);

    while (st13b_adpcm == 0) {
        _ACTWait(1);
    }

    scpFadeIn(8.0f);

    stgmgrNextStagePreLoadForceStageSet(exitData[(&stageData[stage_no])->ent[3]].nextStage);
    stgmgrNextStagePreLoadForceNoCancel(1);

    stage_SetAnimation(465, 1, 0);
    stage_SetAnimation(29, 1, 0);
    stage_SetAnimation(16, 1, 0);
    scpPlayMot((void *)boyGObj, 285);
    scpPlayMot(scpSearchGobj(2080), 1020);
    scpSearchGobj(2080)->active = 1;
    scpPlayMot(scpSearchGobj(2081), 1042);
    scpSearchGobj(2081)->active = 1;
    scpPlayMot(scpSearchGobj(2082), 1066);
    scpSearchGobj(2082)->active = 1;

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 2.7f);

    AdpcmPlay(st13b_adpcm->stream);

    while (stage_ContinueAnimation(465, 466) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(22, 1, 0);
    stage_SetAnimation(17, 1, 0);
    scpPlayMot((void *)boyGObj, 286);
    scpPlayMot(scpSearchGobj(2080), 1021);
    scpPlayMot(scpSearchGobj(2081), 1043);
    scpPlayMot(scpSearchGobj(2082), 1067);

    while (stage_ContinueAnimation(466, 467) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(23, 1, 0);

    while (stage_ContinueAnimation(467, 468) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(24, 1, 0);
    stage_SetAnimation(18, 1, 0);
    scpPlayMot((void *)boyGObj, 287);
    scpPlayMot(scpSearchGobj(2080), 1022);
    scpPlayMot(scpSearchGobj(2081), 1044);
    scpPlayMot(scpSearchGobj(2082), 1068);

    while (stage_ContinueAnimation(468, 469) == 0) {
        _ACTWait(1);
    }

    scpFadeIn(3.0f);

    stage_SetAnimation(25, 1, 0);
    scpPlayMot((void *)boyGObj, 288);
    scpPlayMot(scpSearchGobj(2081), 1045);

    while (stage_ContinueAnimation(469, 470) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(26, 1, 0);
    scpPlayMot((void *)boyGObj, 289);
    scpPlayMot(scpSearchGobj(2080), 1021);
    scpPlayMot(scpSearchGobj(2081), 1043);
    scpPlayMot(scpSearchGobj(2082), 1067);

    while (stage_ContinueAnimation(470, 471) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(19, 1, 0);
    scpPlayMot(scpSearchGobj(2080), 1023);
    scpPlayMot(scpSearchGobj(2081), 1046);
    scpPlayMot(scpSearchGobj(2082), 1069);

    while (stage_ContinueAnimation(471, 472) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 290);
    scpSearchGobj(2080)->active = 0;
    scpSearchGobj(2081)->active = 0;
    scpSearchGobj(2082)->active = 0;

    stage_SetAnimation(29, 0, 0);

    while (stage_CheckAnimationFrame(472, 200, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpFadeOut(3.0f, 0, 0, 0);

    while (scpFadeChk() != 0) {
        _ACTWait(45);
    }

    stage_SetAnimation(472, -1, -2);
    scpFadeIn(3.0f);
    stage_SetAnimation(473, 1, 0);

    while (stage_ContinueAnimation(473, 474) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(21, 1, 0);
    scpPlayMot((void *)boyGObj, 291);
    _ACTWait(60);

    padAct = iosPadActRequest(boyPad, 9);
    padActVolume = 128;
    iosPadActVolumeSet(padAct, 128);

    while (stage_ContinueAnimation(474, 475) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(475, 476) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(20, 1, 0);

    while (stage_ContinueAnimation(476, 477) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 292);
    _ACTWait(130);

    stage_SetAnimation(27, 1, 0);

    while (stage_ContinueAnimation(477, 478) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(478, 479) == 0) {
        _ACTWait(1);
    }

    iosPadActStop(padAct);
    scpPlayMot((void *)boyGObj, 293);

    while (stage_ContinueAnimation(479, 480) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 291);

    while (stage_ContinueAnimation(480, 481) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 295);

    while (stage_ContinueAnimation(481, 482) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 296);
    iosPadActRequest(boyPad, 16);

    while (stage_ContinueAnimation(482, 483) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 297);

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

    while (stage_ContinueAnimation(483, 484) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 298);

    while (stage_ContinueAnimation(484, 485) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 299);

    while (stage_ContinueAnimation(485, 486) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 300);

    while (stage_CheckAnimationFrame(486, 150, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    demoEnd = 1;
}

static void actSt13bConte02_aug6(GObj *volatile self)
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

    stage_SetAnimation(514, 1, 0);
    scpPlayMot((void *)boyGObj, 276);
    scpPlayMot(scpSearchGobj(2172), 942);
    scpSearchGobj(2172)->active = 1;
    scpPlayMot(scpSearchGobj(2173), 964);
    scpSearchGobj(2173)->active = 1;
    scpPlayMot(scpSearchGobj(2174), 988);
    scpSearchGobj(2174)->active = 1;
    _ACTWait(1);

    stage_SetAnimation(33, 1, 0);
    stage_SetAnimation(20, 1, 0);

    _ACTWait(
        ico_d2i(ico_dmul(ico_i2d((60 - systemStatus[0] * 10) / systemStatus[1]), ICO_D(0.15))));

    AdpcmPlay(st13b_adpcm->stream);

    while (stage_ContinueAnimation(514, 515) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 277);
    scpPlayMot(scpSearchGobj(2172), 943);
    scpPlayMot(scpSearchGobj(2173), 965);
    scpPlayMot(scpSearchGobj(2174), 989);
    _ACTWait(1);

    stage_SetAnimation(26, 1, 0);
    stage_SetAnimation(21, 1, 0);

    while (stage_ContinueAnimation(515, 516) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    stage_SetAnimation(27, 1, 0);

    while (stage_ContinueAnimation(516, 517) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 278);
    scpPlayMot(scpSearchGobj(2172), 944);
    scpPlayMot(scpSearchGobj(2173), 966);
    scpPlayMot(scpSearchGobj(2174), 990);
    _ACTWait(1);

    stage_SetAnimation(28, 1, 0);
    stage_SetAnimation(22, 1, 0);

    while (stage_ContinueAnimation(517, 518) == 0) {
        _ACTWait(1);
    }

    scpFadeIn(3.0f);

    scpPlayMot((void *)boyGObj, 279);
    scpPlayMot(scpSearchGobj(2173), 967);
    _ACTWait(1);

    stage_SetAnimation(29, 1, 0);

    while (stage_ContinueAnimation(518, 519) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 280);
    scpPlayMot(scpSearchGobj(2172), 943);
    scpPlayMot(scpSearchGobj(2173), 965);
    scpPlayMot(scpSearchGobj(2174), 989);
    _ACTWait(1);

    stage_SetAnimation(30, 1, 0);

    while (stage_ContinueAnimation(519, 520) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(2172), 945);
    scpPlayMot(scpSearchGobj(2173), 968);
    scpPlayMot(scpSearchGobj(2174), 991);
    _ACTWait(1);

    stage_SetAnimation(23, 1, 0);

    while (stage_ContinueAnimation(520, 521) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 281);
    scpSearchGobj(2172)->active = 0;
    scpSearchGobj(2173)->active = 0;
    scpSearchGobj(2174)->active = 0;

    stage_SetAnimation(33, 0, 0);

    while (stage_CheckAnimationFrame(521, 200, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpFadeOut(3.0f, 0, 0, 0);

    while (scpFadeChk() != 0) {
        _ACTWait(45);
    }

    stage_SetAnimation(521, -1, -2);
    scpFadeIn(3.0f);
    stage_SetAnimation(522, 1, 0);

    while (stage_ContinueAnimation(522, 523) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 282);
    _ACTWait(1);

    stage_SetAnimation(25, 1, 0);
    _ACTWait(60);

    padAct = iosPadActRequest(boyPad, 9);
    padActVolume = 128;
    iosPadActVolumeSet(padAct, 128);

    while (stage_ContinueAnimation(523, 524) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(524, 525) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(24, 1, 0);

    while (stage_ContinueAnimation(525, 526) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 283);
    _ACTWait(130);

    stage_SetAnimation(31, 1, 0);

    while (stage_ContinueAnimation(526, 527) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(527, 528) == 0) {
        _ACTWait(1);
    }

    iosPadActStop(padAct);
    scpPlayMot((void *)boyGObj, 284);

    while (stage_ContinueAnimation(528, 529) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 282);

    while (stage_ContinueAnimation(529, 530) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 286);
    while (stage_ContinueAnimation(530, 531) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 287);
    iosPadActRequest(boyPad, 16);

    while (stage_ContinueAnimation(531, 532) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 288);

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

    while (stage_ContinueAnimation(532, 533) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 289);

    while (stage_ContinueAnimation(533, 534) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 290);

    while (stage_ContinueAnimation(534, 535) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 291);

    while (stage_CheckAnimationFrame(535, 150, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    demoEnd = 1;
}

void actSt13bConte02(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt13bConte02_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt13bConte02_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt13bConte02_aug6(self);
        return;
    }
    scpPlayStart(boyGObj);
    stgmgrNextStagePreLoadForceStageSet(0);
    scpAdpcmPlayRequestFunc(11, &st13b_adpcm, 0, 1, 0);

    while (st13b_adpcm == 0) {
        _ACTWait(1);
    }

    scpFadeIn(8.0f);

    stgmgrNextStagePreLoadForceStageSet(exitData[(&stageData[stage_no])->ent[3]].nextStage);
    stgmgrNextStagePreLoadForceNoCancel(1);

    stage_SetAnimation(PAL_JPNKORCN(586, 582), 1, 0);
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

    if (!ICO_JPNKORCN) {
        if (systemStatus[0] != 0) {
            _ACTWait(ico_d2i(ico_i2d((60 - systemStatus[0] * 10) /
                                     systemStatus[1]))); /* ee-gcc folded the * 1.0 */
        }
    }

    AdpcmPlay(st13b_adpcm->stream);

    while (stage_ContinueAnimation(PAL_JPNKORCN(586, 582), PAL_JPNKORCN(587, 583)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 283);
    scpPlayMot(scpSearchGobj(2403), 1002);
    scpPlayMot(scpSearchGobj(2404), 1024);
    scpPlayMot(scpSearchGobj(2405), 1048);
    _ACTWait(1);

    stage_SetAnimation(27, 1, 0);
    stage_SetAnimation(22, 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(587, 583), PAL_JPNKORCN(588, 584)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    stage_SetAnimation(28, 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(588, 584), PAL_JPNKORCN(589, 585)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 284);
    scpPlayMot(scpSearchGobj(2403), 1003);
    scpPlayMot(scpSearchGobj(2404), 1025);
    scpPlayMot(scpSearchGobj(2405), 1049);
    _ACTWait(1);

    stage_SetAnimation(29, 1, 0);
    stage_SetAnimation(23, 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(589, 585), PAL_JPNKORCN(590, 586)) == 0) {
        _ACTWait(1);
    }

    scpFadeIn(3.0f);

    scpPlayMot((void *)boyGObj, 285);
    scpPlayMot(scpSearchGobj(2404), 1026);
    _ACTWait(1);

    stage_SetAnimation(30, 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(590, 586), PAL_JPNKORCN(591, 587)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 286);
    scpPlayMot(scpSearchGobj(2403), 1002);
    scpPlayMot(scpSearchGobj(2404), 1024);
    scpPlayMot(scpSearchGobj(2405), 1048);
    _ACTWait(1);

    stage_SetAnimation(31, 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(591, 587), PAL_JPNKORCN(592, 588)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(2403), 1004);
    scpPlayMot(scpSearchGobj(2404), 1027);
    scpPlayMot(scpSearchGobj(2405), 1050);
    _ACTWait(1);

    stage_SetAnimation(24, 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(592, 588), PAL_JPNKORCN(593, 589)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 287);
    scpSearchGobj(2403)->active = 0;
    scpSearchGobj(2404)->active = 0;
    scpSearchGobj(2405)->active = 0;

    stage_SetAnimation(34, 0, 0);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(593, 589), 200, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpFadeOut(3.0f, 0, 0, 0);

    while (scpFadeChk() != 0) {
        _ACTWait(45);
    }

    stage_SetAnimation(PAL_JPNKORCN(593, 589), -1, -2);
    scpFadeIn(3.0f);
    stage_SetAnimation(PAL_JPNKORCN(594, 590), 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(594, 590), PAL_JPNKORCN(595, 591)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 288);
    _ACTWait(1);

    stage_SetAnimation(26, 1, 0);
    _ACTWait(60);

    padAct = iosPadActRequest(boyPad, 9);
    padActVolume = 128;
    iosPadActVolumeSet(padAct, 128);

    while (stage_ContinueAnimation(PAL_JPNKORCN(595, 591), PAL_JPNKORCN(596, 592)) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(PAL_JPNKORCN(596, 592), PAL_JPNKORCN(597, 593)) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(25, 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(597, 593), PAL_JPNKORCN(598, 594)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 289);
    _ACTWait(130);

    stage_SetAnimation(32, 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(598, 594), PAL_JPNKORCN(599, 595)) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(PAL_JPNKORCN(599, 595), PAL_JPNKORCN(600, 596)) == 0) {
        _ACTWait(1);
    }

    iosPadActStop(padAct);
    scpPlayMot((void *)boyGObj, 290);

    while (stage_ContinueAnimation(PAL_JPNKORCN(600, 596), PAL_JPNKORCN(601, 597)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 288);

    while (stage_ContinueAnimation(PAL_JPNKORCN(601, 597), PAL_JPNKORCN(602, 598)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 292);
    _ACTWait(1);

    stage_SetAnimation(32, 1, 208);

    while (stage_ContinueAnimation(PAL_JPNKORCN(602, 598), PAL_JPNKORCN(603, 599)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 293);
    iosPadActRequest(boyPad, 16);

    while (stage_ContinueAnimation(PAL_JPNKORCN(603, 599), PAL_JPNKORCN(604, 600)) == 0) {
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

    while (stage_ContinueAnimation(PAL_JPNKORCN(604, 600), PAL_JPNKORCN(605, 601)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 295);

    while (stage_ContinueAnimation(PAL_JPNKORCN(605, 601), PAL_JPNKORCN(606, 602)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 296);

    while (stage_ContinueAnimation(PAL_JPNKORCN(606, 602), PAL_JPNKORCN(607, 603)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot((void *)boyGObj, 297);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(607, 603), 150, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    conte02End = 1;
}

static void actSt13bSekizoChk_jun26(GObj *volatile self)
{
    float dir[4];

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 || scpGameStat_BoyWeaponkind() != 5 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) == 83) {
        _ACTWait(PAL_JUN26(1, 60));
    }

    scpSekizouCheckPoint();
    scpAdpcmPlayRequestFunc(21, 0, 1, 1, 1);
    scpBoyControlReadDisable = 1;
    lt_switch_layout(51);
    gflagOn(10);
    _ACTWait(60);

    stage_SetAnimation(28, 1, 0);

    sekizo_13b_ntscaug6jun26 = iosPadActRequest(boyPad, 9);
    sekizo_13b_vol_ntscaug6jun26 = 128;
    iosPadActVolumeSet(sekizo_13b_ntscaug6jun26, 128);

    scpPlayStart(boyGObj);
    scpPlayPosSet((void *)boyGObj, 3085.0f, -1338.0f, 0.0f);
    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    scpPlayMot((void *)boyGObj, 253);
    scpPlayWaitMotEnd(boyGObj);
    scpPlayMot((void *)boyGObj, 0);
    scpPlayEnd(boyGObj);

    while (stage_CheckAnimationFinish(28) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActStop(sekizo_13b_ntscaug6jun26);

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
}

void actSt13bSekizoChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt13bSekizoChk_jun26(self);
        return;
    }
    float dir[4];

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 || scpGameStat_BoyWeaponkind() != 5 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) == 75) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
    scpBoyControlReadDisable = 1;
    scpAdpcmPlayRequestFunc(PAL_NTSCAUG6JUN26(19, 18), &sekizo13b, 1, 1, 1);

    while (sekizo13b == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_NTSCAUG6JUN26(33, 32), 1, 0);

    if (ICO_NTSCAUG6JUN26) {
        sekizo_13b_ntscaug6jun26 = iosPadActRequest(boyPad, 9);
        sekizo_13b_vol_ntscaug6jun26 = 128;
        iosPadActVolumeSet(sekizo_13b_ntscaug6jun26, 128);
    } else {
        st13b_yure = iosPadActRequest(boyPad, 9);
        st13b_yure_vol = 128;
        iosPadActVolumeSet(st13b_yure, 128);
    }

    scpPlayStart(boyGObj);
    scpPlayPosSet((void *)boyGObj, 3085.0f, -1338.0f, 0.0f);
    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    scpSekizouCheckPoint();
    scpPlayMot((void *)boyGObj, PAL_NTSCAUG6JUN26(251, 246));
    scpPlayWaitMotEnd(boyGObj);
    scpPlayMot((void *)boyGObj, 0);
    scpPlayEnd(boyGObj);
    gflagOn(10);

    while (stage_CheckAnimationFrame(PAL_NTSCAUG6JUN26(33, 32), 151, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActStop(PAL_NTSCAUG6JUN26(st13b_yure, sekizo_13b_ntscaug6jun26));

    while (stage_CheckAnimationFinish(PAL_NTSCAUG6JUN26(33, 32)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));
    scpBoyControlReadDisable = 0;
}

static void actSt13bSekizo2Chk_jun26(GObj *volatile self)
{
    float dir[4];

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 || scpGameStat_BoyWeaponkind() != 5 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) == 83) {
        _ACTWait(PAL_JUN26(1, 60));
    }

    scpSekizouCheckPoint();
    scpAdpcmPlayRequestFunc(21, 0, 1, 1, 1);
    scpBoyControlReadDisable = 1;
    lt_switch_layout(51);
    gflagOn(11);
    _ACTWait(60);

    stage_SetAnimation(29, 1, 0);

    sekizo_13b_ntscaug6jun26 = iosPadActRequest(boyPad, 9);
    sekizo_13b_vol_ntscaug6jun26 = 128;
    iosPadActVolumeSet(sekizo_13b_ntscaug6jun26, 128);

    scpPlayStart(boyGObj);
    scpPlayPosSet((void *)boyGObj, -1618.0f, 527.0f, 0.0f);
    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    scpPlayMot((void *)boyGObj, 253);
    scpPlayWaitMotEnd(boyGObj);
    scpPlayMot((void *)boyGObj, 0);
    scpPlayEnd(boyGObj);

    while (stage_CheckAnimationFinish(29) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActStop(sekizo_13b_ntscaug6jun26);

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
}

void actSt13bSekizo2Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt13bSekizo2Chk_jun26(self);
        return;
    }
    float dir[4];

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 || scpGameStat_BoyWeaponkind() != 5 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) == 75) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
    scpBoyControlReadDisable = 1;
    scpAdpcmPlayRequestFunc(18, &sekizo13b2, 1, 1, 1);

    while (sekizo13b2 == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_NTSCAUG6JUN26(34, 33), 1, 0);

    if (ICO_NTSCAUG6JUN26) {
        sekizo_13b_ntscaug6jun26 = iosPadActRequest(boyPad, 9);
        sekizo_13b_vol_ntscaug6jun26 = 128;
        iosPadActVolumeSet(sekizo_13b_ntscaug6jun26, 128);
    } else {
        st13b_yure = iosPadActRequest(boyPad, 9);
        st13b_yure_vol = 128;
        iosPadActVolumeSet(st13b_yure, 128);
    }

    scpPlayStart(boyGObj);
    scpPlayPosSet((void *)boyGObj, -1563.0f, 527.0f, 0.0f);
    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    scpSekizouCheckPoint();
    scpPlayMot((void *)boyGObj, PAL_NTSCAUG6JUN26(251, 246));
    scpPlayWaitMotEnd(boyGObj);
    scpPlayMot((void *)boyGObj, 0);
    scpPlayEnd(boyGObj);
    gflagOn(11);

    while (stage_CheckAnimationFrame(PAL_NTSCAUG6JUN26(34, 33), 151, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActStop(PAL_NTSCAUG6JUN26(st13b_yure, sekizo_13b_ntscaug6jun26));

    while (stage_CheckAnimationFinish(PAL_NTSCAUG6JUN26(34, 33)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));
    scpBoyControlReadDisable = 0;
}

static void actSt13bMeetAgain_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(12) == 0) {
        stage_SetAnimation(27, 0, -1);
        stage_SetAnimation(21, 0, 0);
        stage_SetAnimation(30, 0, 0);
        stage_SetAnimation(31, 0, 0);
        stage_SetAnimation(33, 0, 0);

        meetAgain_mes[0].func = actSt13bMeetAgainChk;
        act->mail = meetAgain_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    } else {
        stage_SetAnimation(27, 0, -1);
        stage_SetAnimation(21, 0, 0);
        stage_SetAnimation(30, 0, 0);
        stage_SetAnimation(31, 0, 0);
        stage_SetAnimation(33, 0, 0);
    }
}

void actSt13bMeetAgain(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt13bMeetAgain_jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(12) == 0) {
        if (ICO_NTSCAUG6JUN26) {
            stage_SetAnimation(31, 0, -1);
            stage_SetAnimation(25, 0, 0);
            stage_SetAnimation(34, 0, 0);
        } else {
            stage_SetAnimation(32, 0, -1);
            stage_SetAnimation(26, 0, 0);
        }
        stage_SetAnimation(35, 0, 0);
        if (ICO_NTSCAUG6JUN26) {
            stage_SetAnimation(37, 0, 0);
        } else {
            stage_SetAnimation(36, 0, 0);
            stage_SetAnimation(38, 0, 0);
        }

        meetAgain_mes[0].func = actSt13bMeetAgainChk;
        act->mail = meetAgain_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        if (ICO_NTSCAUG6JUN26) {
            stage_SetAnimation(31, 0, -1);
            stage_SetAnimation(25, 0, 0);
            stage_SetAnimation(34, 0, 0);
        } else {
            stage_SetAnimation(32, 0, -1);
            stage_SetAnimation(26, 0, 0);
        }
        stage_SetAnimation(35, 0, 0);
        if (ICO_NTSCAUG6JUN26) {
            stage_SetAnimation(37, 0, 0);
        } else {
            stage_SetAnimation(36, 0, 0);
            stage_SetAnimation(38, 0, 0);
        }
    }
}

static void actSt13bMeetAgainSub(GObj *volatile self)
{
    stage_SetAnimation(PAL_JPNKORCN(760, 756), 1, 0);

    scpPlayMot((void *)boyGObj, 2);
    scpPlayMot(scpSearchGobj(2464), 971);
    scpPlayMot(scpSearchGobj(2465), 972);
    scpPlayMot(scpSearchGobj(2466), 973);
    scpPlayMot(scpSearchGobj(2467), 974);
    scpPlayMot(scpSearchGobj(2468), 975);
    scpPlayMot(scpSearchGobj(2469), 976);

    while (stage_ContinueAnimation(PAL_JPNKORCN(760, 756), PAL_JPNKORCN(761, 757)) == 0) {
        _ACTWait(1);
    }

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(761, 757)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

static void actSt13bMeetAgainChk_ntsc(GObj *volatile self)
{
    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    scpPlayMot(boyGObj, 0);
    gflagOn(12);

    scpAdpcmPlayRequestFunc(34, &meets_again, 1, 1, 1);

    while (meets_again == 0) {
        _ACTWait(1);
    }

    actCreateSubThread(actConte10a, 21);
}

static inline void actSt13bMeetAgainChk_aug6jun26(GObj *volatile self)
{
    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    if (ICO_JUN26) {
        gflagOn(12);
    } else {
        lt_switch_layout(51);
    }
    scpBoyControlReadDisable = 1;
    if (ICO_JUN26) {
        scpAdpcmPlayRequestFunc(37, 0, 1, 1, 1);
    } else {
        scpPlayStart(boyGObj);
        scpPlayMot(boyGObj, 0);
        gflagOn(12);

        scpAdpcmPlayRequestFunc(34, &meets_again, 1, 1, 1);

        while (meets_again == 0) {
            _ACTWait(1);
        }
    }

    actCreateSubThread(actConte10a, 21);
}

void actSt13bMeetAgainChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13bMeetAgainChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13bMeetAgainChk_aug6jun26(self);
        return;
    }
    GProc *th;

    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
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

        stage_SetAnimation(PAL_JPNKORCN(760, 756), 0, -1);
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
    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpAdpcmFadeCloseFunc(&meets_again, 128);
}

static void actConte10a_jun26(GObj *volatile self)
{
    lt_switch_layout(51);
    scpPlayStart(boyGObj);
    stage_SetAnimation(636, 1, 0);

    while (stage_ContinueAnimation(636, 637) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 402);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(637) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actConte10a(GObj *volatile self)
{
    if (ICO_JUN26) {
        actConte10a_jun26(self);
        return;
    }
    stage_SetAnimation(PAL_AUG6JUN26(686, 685), 1, 0);

    while (stage_ContinueAnimation(PAL_AUG6JUN26(686, 685), PAL_AUG6JUN26(687, 686)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 393);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(PAL_AUG6JUN26(687, 686)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);

    if (!ICO_AUG6JUN26) {
        scpAdpcmFadeClose(meets_again, 128);
    }
}

/* PC port: camera-ico2.c's */
void CameraSetCameraSet(int id);

static void actSt13bBoss_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    fightSoundProcessRequestPause();

    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    if (gflagChk(13) == 0) {
        scpSearchGobj(2139)->active = 0;
        scpSearchGobj(2140)->active = 0;
        scpSleepEnemyAll();

        boss_mes[0].func = actSt13bBossChk;
        act->mail = boss_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    } else if (gflagChk(14) == 0) {
        CameraSetCameraSet(5);
        scpAdpcmPlayRequestFunc(39, 0, 1, 0, 1);
    }
}

void actSt13bBoss(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt13bBoss_jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    ScpCallCameraSetTarget(-1886.0f, PAL_AUG6JUN26(625.0f, 651.0f), -4.0f);
    fightSoundProcessRequestPause();

    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    if (gflagChk(13) == 0) {
        if (!ICO_NTSCAUG6JUN26) {
            scpSearchGobj(2470)->active = 0;
        }
        boss_mes[0].func = actSt13bBossChk;
        act->mail = boss_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else if (gflagChk(14) == 0) {
        scpAdpcmPlayRequestFunc(PAL_NTSCAUG6JUN26(37, 36), &boss, 1, 0, 1);
        scpSeEnvMasterVolRate = 0.5f;
    }
}

static void actSt13bBossChk_ntsc(GObj *volatile self)
{
    while (scpTriggerFloorAttr(boyGObj, 0x2000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    gflagOn(13);
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    scpPlayMot(boyGObj, 0);

    scpAdpcmPlayRequestFunc(35, &sd, 1, 1, 1);

    while (sd == 0) {
        _ACTWait(1);
    }

    scpSeEnvMasterVolRate = 0.5f;
    actCreateSubThread(actConte10b, 21);

    while (scpAdpcmCloseChk(meets_again) == 0) {
        _ACTWait(1);
    }

    scpAdpcmPlayRequestFunc(36, &boss, 1, 0, 0);
}

static inline void actSt13bBossChk_jun26(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 300.0f) == 0 || (GOBJ_ACT(boyGObj)->pad.trg & 8) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    gflagOn(13);
    scpBoyControlReadDisable = 1;

    scpAdpcmPlayRequestFunc(38, &boss, 1, 0, 1);

    while (boss == 0) {
        _ACTWait(1);
    }

    actCreateSubThread(actConte10b, 21);
    CameraSetCameraSet(5);
}

static inline void actSt13bBossChk_aug6(GObj *volatile self)
{
    while (scpTriggerFloorAttr(boyGObj, 0x2000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    gflagOn(13);
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    scpPlayMot(boyGObj, 0);

    scpAdpcmPlayRequestFunc(35, &sd, 1, 1, 1);

    while (sd == 0) {
        _ACTWait(1);
    }

    scpSeEnvMasterVolRate = 0.5f;
    actCreateSubThread(actConte10b, 21);

    scpAdpcmPlayRequestFunc(36, &boss, 1, 0, 0);
}

void actSt13bBossChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt13bBossChk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt13bBossChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt13bBossChk_aug6(self);
        return;
    }
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

static void actConte10b_jun26(GObj *volatile self)
{
    scpPlayStart(boyGObj);
    stage_SetAnimation(638, 1, 0);
    scpPlayMot(boyGObj, 403);
    scpSearchGobj(2139)->active = 1;
    scpSearchGobj(2140)->active = 1;
    scpPlayStart(scpSearchGobj(2139));
    scpPlayStart(scpSearchGobj(2140));
    scpPlayMot(scpSearchGobj(2139), 999);
    scpPlayMot(scpSearchGobj(2140), 1000);

    while (stage_CheckAnimationFrame(638, 25, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    stage_SetAnimation(31, 1, 0);

    while (stage_CheckAnimationFrame(638, 130, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    stage_SetAnimation(33, 1, 0);

    while (stage_ContinueAnimation(638, 639) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(32, 1, 0);
    stage_SetAnimation(34, 1, 0);
    scpPlayMot(scpSearchGobj(2140), 1001);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(639) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(scpSearchGobj(2139), 821);
    scpPlayMot(scpSearchGobj(2140), 821);
    scpPlayEnd(boyGObj);
    scpPlayEnd(scpSearchGobj(2139));
    scpPlayEnd(scpSearchGobj(2140));
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    scpWakeupEnemyAll();
}

void actConte10b(GObj *volatile self)
{
    if (ICO_JUN26) {
        actConte10b_jun26(self);
        return;
    }
    stage_SetAnimation(PAL_AUG6JUN26(688, 687), 1, 0);

    while (stage_CheckAnimationFinish(PAL_AUG6JUN26(688, 687)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    CapsuleGhostBossStart();

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 2);

    while (boss == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(boss->stream);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static void actSt13bBossAfterChk_ntsc(GObj *volatile self)
{
    while (InqCapsuleGhostBossEnd() == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    scpPlayMot(boyGObj, 0);
    gflagOn(14);

    if (boss != 0) {
        soundDataClose(boss);
    }

    _ACTWait(120);

    scpAdpcmPlayRequestFunc(37, &boss_dead, 1, 1, 1);

    while (boss_dead == 0) {
        _ACTWait(1);
    }

    actCreateSubThread(actConte10c, 21);
}

static inline void actSt13bBossAfterChk_jun26(GObj *volatile self)
{
    while (actEnemyFlagCheckDead(scpSearchGobj(2139)) == 0 ||
           actEnemyFlagCheckDead(scpSearchGobj(2140)) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    gflagOn(14);

    if (boss != 0) {
        soundDataClose(boss);
    }

    _ACTWait(120);

    CameraSetCameraSet(4);
    scpAdpcmPlayRequestFunc(40, 0, 1, 1, 1);
    actCreateSubThread(actConte10c, 21);
}

static inline void actSt13bBossAfterChk_aug6(GObj *volatile self)
{
    while (InqCapsuleGhostBossEnd() == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    scpPlayMot(boyGObj, 0);
    gflagOn(14);

    if (boss != 0) {
        soundDataClose(boss);
    }

    _ACTWait(120);

    scpAdpcmPlayRequestFunc(37, &boss_dead, 1, 1, 1);

    while (boss_dead == 0) {
        _ACTWait(1);
    }

    actCreateSubThread(actConte10c, 21);
}

void actSt13bBossAfterChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt13bBossAfterChk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt13bBossAfterChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt13bBossAfterChk_aug6(self);
        return;
    }
    GProc *th;

    while (InqCapsuleGhostBossEnd() == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
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

        stage_SetAnimation(PAL_JPNKORCN(763, 759), 0, -1);
        stage_SetAnimation(35, 0, -1);
        scpFadeIn(3.0f);
    }

    scpPlayMot((void *)boyGObj, 0);
    scpPlayEnd(boyGObj);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
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

static void actSt13bElevDown_ntsc(GObj *volatile self)
{
    lt_switch_layout(51);
    gflagOn(15);
    scpAdpcmPlayRequestFunc(77, &st13b_down, 0, 1, 1);

    while (st13b_down == 0) {
        _ACTWait(1);
    }

    preload(3);

    stage_SetAnimation(39, 1, 235);

    while (stage_CheckAnimationFrame(39, 321, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 17);

    while (stage_CheckAnimationFrame(39, 350, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    st13b_yure = iosPadActRequest(boyPad, 9);
    st13b_yure_vol = 128;
    iosPadActVolumeSet(st13b_yure, 128);

    while (stage_CheckAnimationFrame(39, 450, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(st13b_yure);

    RequestStageChange(3, boyGObj, 0, 2.0f, 4.0f);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static void actSt13bElevDown_aug6jun26(GObj *volatile self)
{
    lt_switch_layout(51);
    gflagOn(15);
    scpAdpcmPlayRequestFunc(PAL_JUN26(77, 72), &st13b_down, 0, 1, 1);

    while (st13b_down == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JUN26(39, 35), 1, 235);

    while (stage_CheckAnimationFrame(PAL_JUN26(39, 35), 321, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 17);

    while (stage_CheckAnimationFrame(PAL_JUN26(39, 35), 350, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    st13b_yure = iosPadActRequest(boyPad, 9);
    st13b_yure_vol = 128;
    iosPadActVolumeSet(st13b_yure, 128);

    while (stage_CheckAnimationFrame(PAL_JUN26(39, 35), 450, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(st13b_yure);

    RequestStageChange(3, boyGObj, 0, 2.0f, 4.0f);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actSt13bElevDown(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13bElevDown_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13bElevDown_aug6jun26(self);
        return;
    }
    GProc *th;

    lt_switch_layout(PAL_JPNKORCN(55, 52));
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
    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

static void actSt13bElevUp_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(282) != 0) {
        scpFadeOut(255.0f, 0, 0, 0);
        stage_SetAnimation(35, 0, 235);
        _ACTWait(10);
        lt_switch_layout(51);
        scpBoyControlReadDisable = 1;
        stage_SetAnimation(35, 0, 0);
        elevUp_mes[0].func = actSt13bElevUpChk;
        act->mail = elevUp_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    } else {
        stage_SetAnimation(35, 0, 235);
    }
}

void actSt13bElevUp(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt13bElevUp_jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSC_AUG6JUN26(325, 296, 295)) != 0) {
        lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
        scpBoyControlReadDisable = 1;
        scpFadeOut(255.0f, 0, 0, 0);
        stage_SetAnimation(PAL_NTSCAUG6JUN26(40, 39), 0, 235);
        _ACTWait(10);
        scpBoyControlReadDisable = 1;
        stage_SetAnimation(PAL_NTSCAUG6JUN26(40, 39), 0, 0);
        elevUp_mes[0].func = actSt13bElevUpChk;
        act->mail = elevUp_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_NTSCAUG6JUN26(40, 39), 0, 235);
    }
}

static void actSt13bElevUpChk_ntsc(GObj *volatile self)
{
    scpAdpcmPlayRequestFunc(76, &st13b_up, 1, 1, 1);

    while (st13b_up == 0) {
        _ACTWait(1);
    }

    scpFadeIn(6.0f);

    stage_SetAnimation(39, 1, 0);
    st13b_yure = iosPadActRequest(boyPad, 9);
    st13b_yure_vol = 128;
    iosPadActVolumeSet(st13b_yure, 128);
    while (stage_CheckAnimationFrame(39, 200, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 17);
    iosPadActStop(st13b_yure);
    while (stage_CheckAnimationFrame(39, 234, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    gflagOff(296);
}

static void actSt13bElevUpChk_jun26(GObj *volatile self)
{
    scpAdpcmPlayRequestFunc(71, &st13b_up, 1, 1, 1);

    while (st13b_up == 0) {
        _ACTWait(1);
    }

    scpFadeIn(6.0f);

    stage_SetAnimation(35, 1, 0);
    st13b_yure = iosPadActRequest(boyPad, 9);
    st13b_yure_vol = 128;
    iosPadActVolumeSet(st13b_yure, 128);
    while (stage_CheckAnimationFrame(35, 200, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 17);
    iosPadActStop(st13b_yure);
    while (stage_CheckAnimationFrame(35, 234, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    gflagOff(282);
}

static void actSt13bElevUpChk_aug6(GObj *volatile self)
{
    scpAdpcmPlayRequestFunc(76, &st13b_up, 1, 1, 1);

    while (st13b_up == 0) {
        _ACTWait(1);
    }

    scpFadeIn(6.0f);

    stage_SetAnimation(39, 1, 0);
    st13b_yure = iosPadActRequest(boyPad, 9);
    st13b_yure_vol = 128;
    iosPadActVolumeSet(st13b_yure, 128);
    while (stage_CheckAnimationFrame(39, 200, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    iosPadActRequest(boyPad, 17);
    iosPadActStop(st13b_yure);
    while (stage_CheckAnimationFrame(39, 234, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    gflagOff(295);
}

void actSt13bElevUpChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt13bElevUpChk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt13bElevUpChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt13bElevUpChk_aug6(self);
        return;
    }
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
    lt_switch_layout(PAL_JPNKORCN(54, 51));
    gflagOff(325);
}

static void actSt13bDoorUp_ntsc(GObj *volatile self)
{
    lt_switch_layout(51);
    gflagOn(16);

    _ACTWait(60);

    stage_SetAnimation(40, 1, 0);

    soundSeDefPlay(1197, 0, doorUpSubPos, 1);

    _ACTWait(30);

    soundSeDefPlay(1198, 0, doorUpSubPos, 1);

    while (stage_CheckAnimationFinish(40) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static inline void actSt13bDoorUp_aug6jun26(GObj *volatile self)
{
    lt_switch_layout(51);
    gflagOn(16);

    _ACTWait(60);

    stage_SetAnimation(PAL_JUN26(40, 36), 1, 0);

    soundSeDefPlay(PAL_JUN26(1197, 1171), 0, doorUpSubPos, 1);

    _ACTWait(30);

    soundSeDefPlay(PAL_JUN26(1198, 1172), 0, doorUpSubPos, 1);

    while (stage_CheckAnimationFinish(PAL_JUN26(40, 36)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actSt13bDoorUp(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13bDoorUp_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13bDoorUp_aug6jun26(self);
        return;
    }
    GProc *th;

    lt_switch_layout(PAL_JPNKORCN(55, 52));
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
    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

static void actSt13bElev2Chk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0x4000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
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

    lt_switch_layout(PAL_JPNKORCN(54, 51));

    elev2Chara_mes[0].func = actSt13bElev2CharaChk;
    sub->mail = elev2Chara_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt13bSekizo_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(10) == 0) {
        stage_SetAnimation(32, 0, 0);

        sekizo_mes[0].func = actSt13bSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        stage_SetAnimation(32, 0, -1);
    }
}

static inline void actSt13bSekizo_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(10) == 0) {
        stage_SetAnimation(PAL_JUN26(32, 28), 0, 0);

        sekizo_mes[0].func = actSt13bSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, PAL_JUN26(393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JUN26(32, 28), 0, -1);
    }
}

void actSt13bSekizo(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13bSekizo_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13bSekizo_aug6jun26(self);
        return;
    }
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

static void actSt13bSekizo2_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(11) == 0) {
        stage_SetAnimation(33, 0, 0);

        sekizo2_mes[0].func = actSt13bSekizo2Chk;
        act->mail = sekizo2_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        stage_SetAnimation(33, 0, -1);
    }
}

static inline void actSt13bSekizo2_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(11) == 0) {
        stage_SetAnimation(PAL_JUN26(33, 29), 0, 0);

        sekizo2_mes[0].func = actSt13bSekizo2Chk;
        act->mail = sekizo2_mes;
        ACTSendMailCorrect(self, PAL_JUN26(393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JUN26(33, 29), 0, -1);
    }
}

void actSt13bSekizo2(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13bSekizo2_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13bSekizo2_aug6jun26(self);
        return;
    }
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

static void actSt13bBossAfter_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(14) == 0) {
        stage_SetAnimation(34, 0, 0);

        bossAfter_mes[0].func = actSt13bBossAfterChk;
        act->mail = bossAfter_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        stage_SetAnimation(34, 0, -1);
    }
}

static inline void actSt13bBossAfter_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(14) == 0) {
        stage_SetAnimation(PAL_JUN26(34, 30), 0, 0);

        bossAfter_mes[0].func = actSt13bBossAfterChk;
        act->mail = bossAfter_mes;
        ACTSendMailCorrect(self, PAL_JUN26(393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JUN26(34, 30), 0, -1);
    }
}

void actSt13bBossAfter(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13bBossAfter_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13bBossAfter_aug6jun26(self);
        return;
    }
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

static void actSt13bStoneGirl_ntsc(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (current_stage_no == 46) {
        scpPlayMot(scpSearchGobj(2256), 763);
        scpSearchGobj(2256)->active = 1;
    }
}

static inline void actSt13bStoneGirl_aug6jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (current_stage_no == 46) {
        scpPlayMot(scpSearchGobj(PAL_JUN26(2231, 2138)), PAL_JUN26(765, 789));
        scpSearchGobj(PAL_JUN26(2231, 2138))->active = 1;
    }
}

void actSt13bStoneGirl(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13bStoneGirl_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13bStoneGirl_aug6jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (current_stage_no == 46) {
        scpPlayMot(scpSearchGobj(2462), 808);
        scpSearchGobj(2462)->active = 1;
    }
}

static void actSt13bElev_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    elev_mes[0].func = actSt13bElevMain;
    act->mail = elev_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt13bElev_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    elev_mes[0].func = actSt13bElevMain;
    act->mail = elev_mes;
    ACTSendMailCorrect(self, PAL_JUN26(393, 377));
    _ACTWait(0);
}

void actSt13bElev(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13bElev_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13bElev_aug6jun26(self);
        return;
    }
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

static void actSt13bDoor_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(16) == 0) {
        stage_SetAnimation(40, 0, 0);

        door_mes[0].func = actSt13bDoorMain;
        act->mail = door_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        stage_SetAnimation(40, 0, -1);
    }
}

static inline void actSt13bDoor_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(16) == 0) {
        stage_SetAnimation(PAL_JUN26(40, 36), 0, 0);

        door_mes[0].func = actSt13bDoorMain;
        act->mail = door_mes;
        ACTSendMailCorrect(self, PAL_JUN26(393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JUN26(40, 36), 0, -1);
    }
}

void actSt13bDoor(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13bDoor_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13bDoor_aug6jun26(self);
        return;
    }
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

static void actBossTest_ntsc(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    stage_SetAnimation(31, 0, -1);
    stage_SetAnimation(25, 0, 0);
    stage_SetAnimation(34, 0, 0);
    stage_SetAnimation(35, 0, 0);
    stage_SetAnimation(37, 0, 0);
}

static inline void actBossTest_aug6jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    stage_SetAnimation(PAL_JUN26(31, 27), 0, -1);
    stage_SetAnimation(PAL_JUN26(25, 21), 0, 0);
    stage_SetAnimation(PAL_JUN26(34, 30), 0, 0);
    stage_SetAnimation(PAL_JUN26(35, 31), 0, 0);
    stage_SetAnimation(PAL_JUN26(37, 33), 0, 0);
}

void actBossTest(GObj *volatile self)
{
    if (ICO_NTSC) {
        actBossTest_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actBossTest_aug6jun26(self);
        return;
    }
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

static void actConte10c_ntsc(GObj *volatile self)
{
    stage_SetAnimation(689, 1, 0);

    while (stage_CheckAnimationFrame(689, 300, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    stage_SetAnimation(34, 1, 0);

    while (stage_CheckAnimationFinish(689) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static inline void actConte10c_aug6jun26(GObj *volatile self)
{
    if (ICO_JUN26) {
        stage_SetAnimation(640, 1, 0);
        stage_SetAnimation(30, 1, 0);
        scpPlayMot(boyGObj, 404);
    } else {
        stage_SetAnimation(688, 1, 0);
    }

    while (PAL_JUN26(stage_CheckAnimationFrame(688, 300, 0), stage_ContinueAnimation(640, 641)) ==
           0) {
        _ACTWait(1);
    }
    if (!ICO_JUN26) {
        _ACTWait(1);
    }

    if (ICO_JUN26) {
        SetCameraFlag_LwsCutBack();
    } else {
        stage_SetAnimation(34, 1, 0);
    }

    while (stage_CheckAnimationFinish(PAL_JUN26(688, 641)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actConte10c(GObj *volatile self)
{
    if (ICO_NTSC) {
        actConte10c_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actConte10c_aug6jun26(self);
        return;
    }
    stage_SetAnimation(PAL_JPNKORCN(763, 759), 1, 0);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(763, 759), 300, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    stage_SetAnimation(35, 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(763, 759)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

inline void actSt13bExit(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    exit_mes[0].func = actSt13bExitChk;
    act->mail = exit_mes;
    ACTSendMailCorrect(self, PAL_JUN26(393, 377));
    _ACTWait(0);
}

inline void actSt13bExitChk(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 400.0f) == 0 || gflagChk(PAL_JUN26(295, 282)) != 0) {
        _ACTWait(1);
    }

    RequestStageChange(3, boyGObj, 0, 2.0f, 4.0f);
}

void actSt13bElevMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = elevMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

static void actSt13bElevSwitch_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    scpBoyControlReadDisable = 1;

    elevSwitch_mes[0].func = actSt13bElevDown;
    sub->mail = elevSwitch_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt13bElevSwitch_aug6jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    scpBoyControlReadDisable = 1;

    elevSwitch_mes[0].func = actSt13bElevDown;
    sub->mail = elevSwitch_mes;
    ACTSendMailCorrect(self, PAL_JUN26(393, 377));
    _ACTWait(0);
}

void actSt13bElevSwitch(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13bElevSwitch_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13bElevSwitch_aug6jun26(self);
        return;
    }
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

static inline void actSt13bDoorMain_aug6jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 0;
    sub->mainMail = doorMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt13bDoorMain(GObj *volatile self)
{
    if (ICO_AUG6JUN26) {
        actSt13bDoorMain_aug6jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = doorMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

static void actSt13bDoorSwitch_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    scpBoyControlReadDisable = 1;

    doorSwitch_mes[0].func = actSt13bDoorUp;
    sub->mail = doorSwitch_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt13bDoorSwitch_aug6jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    scpBoyControlReadDisable = 1;

    doorSwitch_mes[0].func = actSt13bDoorUp;
    sub->mail = doorSwitch_mes;
    ACTSendMailCorrect(self, PAL_JUN26(393, 377));
    _ACTWait(0);
}

void actSt13bDoorSwitch(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13bDoorSwitch_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13bDoorSwitch_aug6jun26(self);
        return;
    }
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
