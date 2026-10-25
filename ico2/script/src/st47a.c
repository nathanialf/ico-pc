#include "st47a.h"
#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
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
#include "StageAnimation.h"
#include "attackCheckBoundary.h"
#include "item.h"
#include "motionManager2.h"
#include <libvu0.h>
#include <string.h>
#include "typedef.h"
#include "script.h"
#include "main.h"

static void actSt47aHint2OnChk(GObj *volatile self);
static void actSt47aRopeSub(GObj *volatile self);

void actSt47aInit(void)
{
    if (gflagChk(PAL_NTSCAUG6_JUN26(47, 44, 43)) != 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(166, 162, 145, 144, 131), 0, 0);
        SetWayGroupActive(33, 1);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(165, 161, 144, 143, 130), 0, 0);
        SetWayGroupActive(33, 0);
    }

    if (gflagChk(PAL_NTSCAUG6_JUN26(48, 45, 44)) != 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(168, 164, 147, 146, 133), 0, 0);
        SetWayGroupActive(34, 1);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(167, 163, 146, 145, 132), 0, 0);
        SetWayGroupActive(34, 0);
    }
}

void actSt47aEnd(void)
{
    if (girlGObj != 0) {
        if (gflagChk(PAL_NTSCAUG6JUN26(52, 49)) == 0) {
            gflagOn(PAL_NTSC_AUG6JUN26(391, 358, 357));
        }
    }
}

/* .sdata: the statue and wing stream handles and the statue's shake. */
SqEntry *sekizo47a = 0;

SqEntry *hane1up = 0;

SqEntry *hane2up = 0;

SqEntry *hane1down = 0;

SqEntry *hane2down = 0;

int sekizo_47a = 0;

unsigned char sekizo_47a_vol = 0;

inline void actSt47aSekizo1Event(int x);
inline void actSt47aGirlWay(GObj *volatile self);
inline void actSt47aSekizo2Event(int x);
inline void actSt47aSekizo3Event(int x);
inline void actSt47aSekizo4Event(int x);
inline void actSt47aHane1Main(GObj *volatile self);
inline void actSt47aHane1Switch(GObj *volatile self);
inline void actSt47aHane1Down(GObj *volatile self);
inline void actSt47aHane1_1Girl(GObj *volatile self);
inline void actSt47aHane1_2Girl(GObj *volatile self);
inline void actSt47aHane2Main(GObj *volatile self);
inline void actSt47aHane2Switch(GObj *volatile self);
inline void actSt47aHane2Down(GObj *volatile self);
inline void actSt47aHane2Girl(GObj *volatile self);
inline void actSt47aBarricadeEvent(int x);
inline void actSt47aBarricadeChk(GObj *volatile self);
inline void actSt47aExitChk(GObj *volatile self);
inline void actSt47aExit2Chk(GObj *volatile self);
inline void actSt47aEneChk(GObj *volatile self);
void actSt47aSekizo1Chk(GObj *volatile self);
void actSt47aHane1Up(GObj *volatile self);
void actSt47aHane2Up(GObj *volatile self);
void actSt47aRopeChk(GObj *volatile self);

static void actSt47aSekizo1Chk_jun26(GObj *volatile self)
{
    /* the sound handle, which the sound subsystem owns; it is kept across
       the whole cutscene and read again for soundSeDefStop */
    volatile int se;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) == 83) {
        _ACTWait(1);
    }

    scpSekizouCheckPoint();

    scpAdpcmPlayRequestFunc(21, 0, 1, 1, 1);

    scpKillEnemyAll();
    scpMaskGeneratorAll();

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    brainLockGirl();

    gflagOn(39);

    _ACTWait(60);

    stage_SetAnimation(126, 1, 0);

    sekizo_47a = iosPadActRequest(boyPad, 9);
    sekizo_47a_vol = 128;
    iosPadActVolumeSet(sekizo_47a, 128);

    se = soundSeDefPlay(1167, 0, 0, 1);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 521);

    scpPlayPosSet(girlGObj, 1750.0f, -272.0f, 0.0f);
    scpPlayPosSet(boyGObj, 1750.0f, -272.0f, 50.0f);

    _ACTWait(1);
    {
        Vec16 v;

        sceVu0SubVector(v.f, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
        scpPlayMotDir(girlGObj, v.f);
        scpBoyControlReadDisable = 1;
        sceVu0SubVector(v.f, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
        scpPlayMotDir(boyGObj, v.f);

        scpPlayMot(girlGObj, 584);
        scpPlayWaitMotEnd(girlGObj);

        soundSeDefStop(se);
    }

    while (stage_CheckAnimationFrame(126, 151, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(sekizo_47a);

    scpPlayMot(girlGObj, 521);
    scpPlayEnd(girlGObj);

    actCreateSubThread(actSt47aGirlWay, 21);

    _ACTWait(30);

    scpPlayMot(boyGObj, 254);
    scpPlayWaitMotEnd(boyGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
}

static void actSt47aSekizo1Chk_aug6(GObj *volatile self)
{
    /* the sound handle, which the sound subsystem owns; it is kept across
       the whole cutscene and read again for soundSeDefStop */
    volatile int se;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) == 75) {
        _ACTWait(1);
    }

    scpAdpcmPlayRequestFunc(18, &sekizo47a, 1, 1, 1);
    while (sekizo47a == 0) {
        _ACTWait(1);
    }

    scpKillEnemyAll();
    scpMaskGeneratorAll();

    gflagOn(356);

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    brainLockGirl();

    _ACTWait(60);

    stage_SetAnimation(139, 1, 0);

    ReviveAllCarryableItemsWithNonSleepFrame(300);

    sekizo_47a = iosPadActRequest(boyPad, 9);
    sekizo_47a_vol = 128;
    iosPadActVolumeSet(sekizo_47a, 128);

    se = soundSeDefPlay(1193, 0, 0, 1);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);

    scpPlayPosSet(girlGObj, 1750.0f, -272.0f, 0.0f);
    scpPlayPosSet(boyGObj, 1750.0f, -272.0f, 50.0f);

    _ACTWait(1);
    {
        Vec16 v;

        sceVu0SubVector(v.f, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
        scpPlayMotDir(girlGObj, v.f);
        scpBoyControlReadDisable = 1;
        sceVu0SubVector(v.f, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
        scpPlayMotDir(boyGObj, v.f);

        scpSekizouCheckPoint();

        scpPlayMot(girlGObj, 573);
        scpPlayWaitMotEnd(girlGObj);

        gflagOn(40);

        soundSeDefStop(se);
    }

    while (stage_CheckAnimationFrame(139, 151, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(sekizo_47a);

    scpPlayMot(girlGObj, 514);
    scpPlayEnd(girlGObj);

    actCreateSubThread(actSt47aGirlWay, 21);

    _ACTWait(30);

    scpPlayMot(boyGObj, 247);
    scpPlayWaitMotEnd(boyGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    ScpCallCameraSetTarget(-3000.0f, 272.0f, 0.0f);
}

static void actSt47aSekizo1Chk_ntsc(GObj *volatile self)
{
    /* the sound handle, which the sound subsystem owns; it is kept across
       the whole cutscene and read again for soundSeDefStop */
    volatile int se;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) == 75) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    brainLockGirl();

    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(18, &sekizo47a, 1, 1, 1);
    while (sekizo47a == 0) {
        _ACTWait(1);
    }

    gflagOn(357);

    stage_SetAnimation(140, 1, 0);

    scpKillEnemyAll();
    scpMaskGeneratorAll();

    ReviveAllCarryableItemsWithNonSleepFrame(300);

    sekizo_47a = iosPadActRequest(boyPad, 9);
    sekizo_47a_vol = 128;
    iosPadActVolumeSet(sekizo_47a, 128);

    se = soundSeDefPlay(1193, 0, 0, 1);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);

    scpPlayPosSet(girlGObj, 1750.0f, -272.0f, 0.0f);
    scpPlayPosSet(boyGObj, 1750.0f, -272.0f, 50.0f);

    _ACTWait(1);
    {
        Vec16 v;

        sceVu0SubVector(v.f, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
        scpPlayMotDir(girlGObj, v.f);
        scpBoyControlReadDisable = 1;
        sceVu0SubVector(v.f, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
        scpPlayMotDir(boyGObj, v.f);

        scpSekizouCheckPoint();

        scpPlayMot(girlGObj, 572);
        scpPlayWaitMotEnd(girlGObj);

        gflagOn(40);

        soundSeDefStop(se);
    }

    while (stage_CheckAnimationFrame(140, 151, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(sekizo_47a);

    scpPlayMot(girlGObj, 514);
    scpPlayEnd(girlGObj);

    actCreateSubThread(actSt47aGirlWay, 21);

    _ACTWait(30);

    scpPlayMot(boyGObj, 247);
    scpPlayWaitMotEnd(boyGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    ScpCallCameraSetTarget(-3000.0f, 272.0f, 0.0f);
}

void actSt47aSekizo1Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt47aSekizo1Chk_jun26(self);
        return;
    }
    if (ICO_AUG6) {
        actSt47aSekizo1Chk_aug6(self);
        return;
    }
    if (ICO_NTSC) {
        actSt47aSekizo1Chk_ntsc(self);
        return;
    }
    /* the sound handle, which the sound subsystem owns; it is kept across
       the whole cutscene and read again for soundSeDefStop */
    volatile int se;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) == 75) {
        _ACTWait(1);
    }

    if (ICO_NTSC) {
        lt_switch_layout(51);
        scpBoyControlReadDisable = 1;
        brainLockGirl();

        scpSleepEnemyAll();

        scpAdpcmPlayRequestFunc(18, &sekizo47a, 1, 1, 1);
    } else if (ICO_AUG6JUN26) {
        scpAdpcmPlayRequestFunc(18, &sekizo47a, 1, 1, 1);
    } else {
        lt_switch_layout(PAL_JPNKORCN(55, 52));
        scpBoyControlReadDisable = 1;
        brainLockGirl();

        scpSleepEnemyAll();

        scpAdpcmPlayRequestFunc(19, &sekizo47a, 1, 1, 1);
    }
    while (sekizo47a == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSC) {
        gflagOn(357);

        stage_SetAnimation(140, 1, 0);
    } else if (ICO_AUG6JUN26) {
    } else {
        gflagOn(390);

        stage_SetAnimation(PAL_JPNKORCN(163, 159), 1, 0);
    }

    scpKillEnemyAll();
    scpMaskGeneratorAll();

    ReviveAllCarryableItemsWithNonSleepFrame(300);

    sekizo_47a = iosPadActRequest(boyPad, 9);
    sekizo_47a_vol = 128;
    iosPadActVolumeSet(sekizo_47a, 128);

    se = soundSeDefPlay(PAL_NTSCAUG6JUN26(1217, 1193), 0, 0, 1);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, PAL_NTSCAUG6JUN26(532, 514));

    scpPlayPosSet(girlGObj, 1750.0f, -272.0f, 0.0f);
    scpPlayPosSet(boyGObj, 1750.0f, -272.0f, 50.0f);

    _ACTWait(1);
    {
        Vec16 v;

        sceVu0SubVector(v.f, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
        scpPlayMotDir(girlGObj, v.f);
        scpBoyControlReadDisable = 1;
        sceVu0SubVector(v.f, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
        scpPlayMotDir(boyGObj, v.f);

        scpSekizouCheckPoint();

        scpPlayMot(girlGObj, PAL_NTSC_AUG6JUN26(645, 572, 573));
        scpPlayWaitMotEnd(girlGObj);

        gflagOn(PAL_NTSCAUG6JUN26(43, 40));

        soundSeDefStop(se);
    }

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6JUN26(163, 159, 140, 139), 151, 0) ==
           0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(sekizo_47a);

    scpPlayMot(girlGObj, PAL_NTSCAUG6JUN26(532, 514));
    scpPlayEnd(girlGObj);

    actCreateSubThread(actSt47aGirlWay, 21);

    _ACTWait(30);

    scpPlayMot(boyGObj, PAL_NTSCAUG6JUN26(252, 247));
    scpPlayWaitMotEnd(boyGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    ScpCallCameraSetTarget(-3000.0f, 272.0f, 0.0f);

    if (!ICO_NTSCAUG6JUN26) {
        while (stage_CheckAnimationFinish(PAL_JPNKORCN(163, 159)) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        scpWakeupEnemyAll();

        scpBoyControlReadDisable = 0;
        lt_switch_layout(PAL_JPNKORCN(54, 51));
    }
}

/* the shared pad-state array (op.c's PadState, GsBase.c's GsbPad): 0x58 per
   pad, trg at 0x4. */

static ActMail sekizo1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail sekizo1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail sekizo1_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail sekizo1_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define sekizo1_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(sekizo1_mes_pal, sekizo1_mes_ntsc, sekizo1_mes_aug6, sekizo1_mes_jun26)

static ActMail hane1Main_mes_pal[2] = {{406, actSt47aHane1Switch}, {429}}; /* derived name */

static ActMail hane1Main_mes_ntsc[2] = {{376, actSt47aHane1Switch}, {396}}; /* derived name */

static ActMail hane1Main_mes_aug6[2] = {{372, actSt47aHane1Switch}, {392}}; /* derived name */

static ActMail hane1Main_mes_jun26[2] = {{356, actSt47aHane1Switch}, {376}}; /* derived name */

#define hane1Main_mes                                                                              \
    PAL_NTSC_AUG6_JUN26(hane1Main_mes_pal, hane1Main_mes_ntsc, hane1Main_mes_aug6,                 \
                        hane1Main_mes_jun26)

static ActMail hane1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail hane1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail hane1_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail hane1_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define hane1_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(hane1_mes_pal, hane1_mes_ntsc, hane1_mes_aug6, hane1_mes_jun26)

static ActMail hane1SwitchUp_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail hane1SwitchUp_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail hane1SwitchUp_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail hane1SwitchUp_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define hane1SwitchUp_mes                                                                          \
    PAL_NTSC_AUG6_JUN26(hane1SwitchUp_mes_pal, hane1SwitchUp_mes_ntsc, hane1SwitchUp_mes_aug6,     \
                        hane1SwitchUp_mes_jun26)

static ActMail hane1SwitchDown_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail hane1SwitchDown_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail hane1SwitchDown_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail hane1SwitchDown_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define hane1SwitchDown_mes                                                                        \
    PAL_NTSC_AUG6_JUN26(hane1SwitchDown_mes_pal, hane1SwitchDown_mes_ntsc,                         \
                        hane1SwitchDown_mes_aug6, hane1SwitchDown_mes_jun26)

static ActMail hane1Down_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail hane1Down_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail hane1Down_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define hane1Down_mes                                                                              \
    PAL_NTSC_AUG6JUN26(hane1Down_mes_pal, hane1Down_mes_ntsc, hane1Down_mes_aug6jun26)

static ActMail hane1Up_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail hane1Up_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail hane1Up_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define hane1Up_mes PAL_NTSC_AUG6JUN26(hane1Up_mes_pal, hane1Up_mes_ntsc, hane1Up_mes_aug6jun26)

static ActMail hane2Main_mes_pal[2] = {{407, actSt47aHane2Switch}, {429}}; /* derived name */

static ActMail hane2Main_mes_ntsc[2] = {{377, actSt47aHane2Switch}, {396}}; /* derived name */

static ActMail hane2Main_mes_aug6[2] = {{373, actSt47aHane2Switch}, {392}}; /* derived name */

static ActMail hane2Main_mes_jun26[2] = {{357, actSt47aHane2Switch}, {376}}; /* derived name */

#define hane2Main_mes                                                                              \
    PAL_NTSC_AUG6_JUN26(hane2Main_mes_pal, hane2Main_mes_ntsc, hane2Main_mes_aug6,                 \
                        hane2Main_mes_jun26)

static ActMail hane2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail hane2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail hane2_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail hane2_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define hane2_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(hane2_mes_pal, hane2_mes_ntsc, hane2_mes_aug6, hane2_mes_jun26)

static ActMail hane2SwitchUp_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail hane2SwitchUp_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail hane2SwitchUp_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail hane2SwitchUp_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define hane2SwitchUp_mes                                                                          \
    PAL_NTSC_AUG6_JUN26(hane2SwitchUp_mes_pal, hane2SwitchUp_mes_ntsc, hane2SwitchUp_mes_aug6,     \
                        hane2SwitchUp_mes_jun26)

static ActMail hane2SwitchDown_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail hane2SwitchDown_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail hane2SwitchDown_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail hane2SwitchDown_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define hane2SwitchDown_mes                                                                        \
    PAL_NTSC_AUG6_JUN26(hane2SwitchDown_mes_pal, hane2SwitchDown_mes_ntsc,                         \
                        hane2SwitchDown_mes_aug6, hane2SwitchDown_mes_jun26)

static ActMail hane2Down_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail hane2Down_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail hane2Down_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define hane2Down_mes                                                                              \
    PAL_NTSC_AUG6JUN26(hane2Down_mes_pal, hane2Down_mes_ntsc, hane2Down_mes_aug6jun26)

static ActMail hane2Up_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail hane2Up_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail hane2Up_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define hane2Up_mes PAL_NTSC_AUG6JUN26(hane2Up_mes_pal, hane2Up_mes_ntsc, hane2Up_mes_aug6jun26)

static ActMail rope_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail rope_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail rope_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail rope_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define rope_mes PAL_NTSC_AUG6_JUN26(rope_mes_pal, rope_mes_ntsc, rope_mes_aug6, rope_mes_jun26)

static ActMail barricade_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail barricade_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail barricade_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail barricade_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define barricade_mes                                                                              \
    PAL_NTSC_AUG6_JUN26(barricade_mes_pal, barricade_mes_ntsc, barricade_mes_aug6,                 \
                        barricade_mes_jun26)

static ActMail exit_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail exit_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail exit_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail exit_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define exit_mes PAL_NTSC_AUG6_JUN26(exit_mes_pal, exit_mes_ntsc, exit_mes_aug6, exit_mes_jun26)

static ActMail exit2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail exit2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail exit2_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail exit2_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define exit2_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(exit2_mes_pal, exit2_mes_ntsc, exit2_mes_aug6, exit2_mes_jun26)

static ActMail ene_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail ene_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail ene_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail ene_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define ene_mes PAL_NTSC_AUG6_JUN26(ene_mes_pal, ene_mes_ntsc, ene_mes_aug6, ene_mes_jun26)

static ActMail hint2On_mes[2] = {{430}, {429}}; /* derived name */

static void actSt47aHane1Down_ntsc(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    scpAdpcmPlayRequestFunc(62, &hane1down, 1, 1, 1);

    while (hane1down == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(144, 1, 0);

    gflagOn(44);

    while (stage_CheckAnimationFinish(144) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);
    scpWakeupEnemyAll();

    SetWayGroupActive(33, 1);

    hane1Down_mes[0].func = actSt47aHane1Main;
    act->mail = hane1Down_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt47aHane1Down_jun26(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);
    static ActMail hane1Down_mes_l[2] = {{377}, {376}}; /* derived name */

    scpAdpcmPlayRequestFunc(59, 0, 1, 1, 1);

    stage_SetAnimation(130, 1, 0);

    SetWayGroupActive(33, 1);

    gflagOn(43);

    while (stage_CheckAnimationFinish(130) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    hane1Down_mes_l[0].func = actSt47aHane1Main;
    act->mail = hane1Down_mes_l;
    ACTSendMailCorrect(self, 377);
    _ACTWait(0);
}

static inline void actSt47aHane1Down_aug6(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    scpAdpcmPlayRequestFunc(62, &hane1down, 1, 1, 1);

    while (hane1down == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(143, 1, 0);

    SetWayGroupActive(33, 1);

    gflagOn(44);

    while (stage_CheckAnimationFinish(143) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    hane1Down_mes[0].func = actSt47aHane1Main;
    act->mail = hane1Down_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt47aHane1Down(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt47aHane1Down_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt47aHane1Down_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt47aHane1Down_aug6(self);
        return;
    }
    Act *act = GOBJ_ACT(self);

    scpAdpcmPlayRequestFunc(64, &hane1down, 1, 1, 1);

    while (hane1down == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JPNKORCN(165, 161), 1, 0);

    gflagOn(47);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(165, 161)) == 0) {
        if ((pad[0].flags & 0x800) != 0 && scpAdpcmPlayRequestNum() == 0) {
            scpFadeOut(16.0f, 0, 0, 0);

            scpAdpcmFadeCloseFunc(&hane1down, 512);

            while (scpFadeChk() != 0) {
                _ACTWait(1);
            }
            while (lt_fade_status() != 2) {
                _ACTWait(1);
            }

            stage_SetAnimation(PAL_JPNKORCN(165, 161), 0, -1);

            scpFadeIn(3.0f);
            break;
        }
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 0;

    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpWakeupEnemyAll();

    SetWayGroupActive(33, 1);

    hane1Down_mes[0].func = actSt47aHane1Main;
    act->mail = hane1Down_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt47aHane1Up_jun26(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);
    static ActMail hane1Up_mes_l[2] = {{377}, {376}}; /* derived name */

    if (girlGObj != 0) {
        if (scpTriggerFloorAttr(girlGObj, 0x2000000) != 0) {
            actCreateSubThread(actSt47aHane1_1Girl, 21);
        }
        if (scpTriggerFloorAttr(girlGObj, 0x4000000) != 0) {
            actCreateSubThread(actSt47aHane1_2Girl, 21);
        }
    }

    scpAdpcmPlayRequestFunc(60, 0, 1, 1, 1);

    stage_SetAnimation(131, 1, 0);

    SetWayGroupActive(33, 0);

    gflagOff(43);

    while (stage_CheckAnimationFinish(131) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    hane1Up_mes_l[0].func = actSt47aHane1Main;
    act->mail = hane1Up_mes_l;
    ACTSendMailCorrect(self, 377);
    _ACTWait(0);
}

static void actSt47aHane1Up_aug6(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    if (girlGObj != 0) {
        if (scpTriggerFloorAttr(girlGObj, 0x2000000) != 0) {
            actCreateSubThread(actSt47aHane1_1Girl, 21);
        }
        if (scpTriggerFloorAttr(girlGObj, 0x4000000) != 0) {
            actCreateSubThread(actSt47aHane1_2Girl, 21);
        }
    }

    scpAdpcmPlayRequestFunc(64, &hane1up, 1, 1, 1);

    while (hane1up == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(144, 1, 0);

    SetWayGroupActive(33, 0);

    gflagOff(44);

    while (stage_CheckAnimationFinish(144) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    hane1Up_mes[0].func = actSt47aHane1Main;
    act->mail = hane1Up_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

static void actSt47aHane1Up_ntsc(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    if (girlGObj != 0) {
        if (scpTriggerFloorAttr(girlGObj, 0x2000000) != 0) {
            actCreateSubThread(actSt47aHane1_1Girl, 21);
        }
        if (scpTriggerFloorAttr(girlGObj, 0x4000000) != 0) {
            actCreateSubThread(actSt47aHane1_2Girl, 21);
        }
    }

    scpAdpcmPlayRequestFunc(64, &hane1up, 1, 1, 1);

    while (hane1up == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(145, 1, 0);

    gflagOff(44);

    while (stage_CheckAnimationFinish(145) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);
    scpWakeupEnemyAll();

    SetWayGroupActive(33, 0);

    hane1Up_mes[0].func = actSt47aHane1Main;
    act->mail = hane1Up_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

void actSt47aHane1Up(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt47aHane1Up_jun26(self);
        return;
    }
    if (ICO_AUG6) {
        actSt47aHane1Up_aug6(self);
        return;
    }
    if (ICO_NTSC) {
        actSt47aHane1Up_ntsc(self);
        return;
    }
    Act *act = GOBJ_ACT(self);

    if (girlGObj != 0) {
        if (scpTriggerFloorAttr(girlGObj, 0x2000000) != 0) {
            actCreateSubThread(actSt47aHane1_1Girl, 21);
        }
        if (scpTriggerFloorAttr(girlGObj, 0x4000000) != 0) {
            actCreateSubThread(actSt47aHane1_2Girl, 21);
        }
    }

    scpAdpcmPlayRequestFunc(PAL_NTSCAUG6JUN26(66, 64), &hane1up, 1, 1, 1);

    while (hane1up == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSC) {
        stage_SetAnimation(145, 1, 0);

        gflagOff(44);

        while (stage_CheckAnimationFinish(145) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        scpBoyControlReadDisable = 0;

        lt_switch_layout(50);
        scpWakeupEnemyAll();
    } else if (ICO_AUG6JUN26) {
        stage_SetAnimation(144, 1, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(166, 162), 1, 0);

        gflagOff(47);

        SetCameraFlag_LwsCutBack();

        while (stage_CheckAnimationFinish(PAL_JPNKORCN(166, 162)) == 0) {
            if ((pad[0].flags & 0x800) != 0 && scpAdpcmPlayRequestNum() == 0) {
                scpFadeOut(16.0f, 0, 0, 0);

                scpAdpcmFadeCloseFunc(&hane1up, 512);

                while (scpFadeChk() != 0) {
                    _ACTWait(1);
                }
                while (lt_fade_status() != 2) {
                    _ACTWait(1);
                }

                stage_SetAnimation(PAL_JPNKORCN(166, 162), 0, -1);

                scpFadeIn(3.0f);
                break;
            }
            _ACTWait(1);
        }

        scpBoyControlReadDisable = 0;

        lt_switch_layout(PAL_JPNKORCN(54, 51));
        scpWakeupEnemyAll();
    }

    SetWayGroupActive(33, 0);

    hane1Up_mes[0].func = actSt47aHane1Main;
    act->mail = hane1Up_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

static void actSt47aHane2Down_ntsc(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    scpAdpcmPlayRequestFunc(63, &hane2down, 1, 1, 1);

    while (hane2down == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(146, 1, 0);

    gflagOn(45);

    while (stage_CheckAnimationFinish(146) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);
    scpWakeupEnemyAll();

    SetWayGroupActive(34, 1);

    hane2Down_mes[0].func = actSt47aHane2Main;
    act->mail = hane2Down_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt47aHane2Down_jun26(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);
    static ActMail hane2Down_mes_l[2] = {{377}, {376}}; /* derived name */

    scpAdpcmPlayRequestFunc(59, 0, 1, 1, 1);

    stage_SetAnimation(132, 1, 0);

    SetWayGroupActive(34, 1);

    gflagOn(44);

    while (stage_CheckAnimationFinish(132) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    hane2Down_mes_l[0].func = actSt47aHane2Main;
    act->mail = hane2Down_mes_l;
    ACTSendMailCorrect(self, 377);
    _ACTWait(0);
}

static inline void actSt47aHane2Down_aug6(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    scpAdpcmPlayRequestFunc(63, &hane2down, 1, 1, 1);

    while (hane2down == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(145, 1, 0);

    SetWayGroupActive(34, 1);

    gflagOn(45);

    while (stage_CheckAnimationFinish(145) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    hane2Down_mes[0].func = actSt47aHane2Main;
    act->mail = hane2Down_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt47aHane2Down(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt47aHane2Down_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt47aHane2Down_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt47aHane2Down_aug6(self);
        return;
    }
    Act *act = GOBJ_ACT(self);

    scpAdpcmPlayRequestFunc(65, &hane2down, 1, 1, 1);

    while (hane2down == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JPNKORCN(167, 163), 1, 0);

    gflagOn(48);

    SetCameraFlag_LwsCutBack();

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(167, 163)) == 0) {
        if ((pad[0].flags & 0x800) != 0 && scpAdpcmPlayRequestNum() == 0) {
            scpFadeOut(16.0f, 0, 0, 0);

            scpAdpcmFadeCloseFunc(&hane2down, 512);

            while (scpFadeChk() != 0) {
                _ACTWait(1);
            }
            while (lt_fade_status() != 2) {
                _ACTWait(1);
            }

            stage_SetAnimation(PAL_JPNKORCN(167, 163), 0, -1);

            scpFadeIn(3.0f);
            break;
        }
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 0;

    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpWakeupEnemyAll();

    SetWayGroupActive(34, 1);

    hane2Down_mes[0].func = actSt47aHane2Main;
    act->mail = hane2Down_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt47aHane2Up_jun26(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);
    static ActMail hane2Up_mes_l[2] = {{377}, {376}}; /* derived name */

    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        actCreateSubThread(actSt47aHane2Girl, 21);
    }

    scpAdpcmPlayRequestFunc(60, 0, 1, 1, 1);

    stage_SetAnimation(133, 1, 0);

    SetWayGroupActive(34, 0);

    gflagOff(44);

    while (stage_CheckAnimationFinish(133) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    hane2Up_mes_l[0].func = actSt47aHane2Main;
    act->mail = hane2Up_mes_l;
    ACTSendMailCorrect(self, 377);
    _ACTWait(0);
}

static void actSt47aHane2Up_aug6(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        actCreateSubThread(actSt47aHane2Girl, 21);
    }

    scpAdpcmPlayRequestFunc(65, &hane2up, 1, 1, 1);

    while (hane2up == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(146, 1, 0);

    SetWayGroupActive(34, 0);

    gflagOff(45);

    while (stage_CheckAnimationFinish(146) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    hane2Up_mes[0].func = actSt47aHane2Main;
    act->mail = hane2Up_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

static void actSt47aHane2Up_ntsc(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        actCreateSubThread(actSt47aHane2Girl, 21);
    }

    scpAdpcmPlayRequestFunc(65, &hane2up, 1, 1, 1);

    while (hane2up == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(147, 1, 0);

    gflagOff(45);

    while (stage_CheckAnimationFinish(147) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);
    scpWakeupEnemyAll();

    SetWayGroupActive(34, 0);

    hane2Up_mes[0].func = actSt47aHane2Main;
    act->mail = hane2Up_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

void actSt47aHane2Up(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt47aHane2Up_jun26(self);
        return;
    }
    if (ICO_AUG6) {
        actSt47aHane2Up_aug6(self);
        return;
    }
    if (ICO_NTSC) {
        actSt47aHane2Up_ntsc(self);
        return;
    }
    Act *act = GOBJ_ACT(self);

    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0x3000000) != 0) {
        actCreateSubThread(actSt47aHane2Girl, 21);
    }

    scpAdpcmPlayRequestFunc(PAL_NTSCAUG6JUN26(67, 65), &hane2up, 1, 1, 1);

    while (hane2up == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSC) {
        stage_SetAnimation(147, 1, 0);

        gflagOff(45);

        while (stage_CheckAnimationFinish(147) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        scpBoyControlReadDisable = 0;

        lt_switch_layout(50);
        scpWakeupEnemyAll();
    } else if (ICO_AUG6JUN26) {
        stage_SetAnimation(146, 1, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(168, 164), 1, 0);

        gflagOff(48);

        SetCameraFlag_LwsCutBack();

        while (stage_CheckAnimationFinish(PAL_JPNKORCN(168, 164)) == 0) {
            if ((pad[0].flags & 0x800) != 0 && scpAdpcmPlayRequestNum() == 0) {
                scpFadeOut(16.0f, 0, 0, 0);

                scpAdpcmFadeCloseFunc(&hane2up, 512);

                while (scpFadeChk() != 0) {
                    _ACTWait(1);
                }
                while (lt_fade_status() != 2) {
                    _ACTWait(1);
                }

                stage_SetAnimation(PAL_JPNKORCN(168, 164), 0, -1);

                scpFadeIn(3.0f);
                break;
            }
            _ACTWait(1);
        }

        scpBoyControlReadDisable = 0;

        lt_switch_layout(PAL_JPNKORCN(54, 51));
        scpWakeupEnemyAll();
    }

    SetWayGroupActive(34, 0);

    hane2Up_mes[0].func = actSt47aHane2Main;
    act->mail = hane2Up_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

static void actSt47aRope_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(46) == 0) {
        stage_SetAnimation(148, 0, 0);
        scpSearchGobj(460)->active = 0;
        rope_mes[0].func = actSt47aRopeChk;
        act->mail = rope_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        stage_SetAnimation(149, 0, -1);
        scpSearchGobj(460)->active = 1;
    }
}

static inline void actSt47aRope_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_JUN26(46, 45)) == 0) {
        stage_SetAnimation(PAL_JUN26(147, 134), 0, 0);
        scpSearchGobj(PAL_JUN26(459, 442))->active = 0;
        rope_mes[0].func = actSt47aRopeChk;
        act->mail = rope_mes;
        ACTSendMailCorrect(self, PAL_JUN26(393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JUN26(148, 134), 0, -1);
        scpSearchGobj(PAL_JUN26(459, 442))->active = 1;
    }
}

void actSt47aRope(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt47aRope_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt47aRope_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(49) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(169, 165), 0, 0);
        scpSearchGobj(503)->active = 0;
        rope_mes[0].func = actSt47aRopeChk;
        act->mail = rope_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(170, 166), 0, -1);
        scpSearchGobj(503)->active = 1;
        scpSearchGobj(482)->active = 0;
        FinishHint(5);
    }
}

/* PC port: defined below */
static ActMail ropeSwing_mes[2];

void actSt47aRopeSwingChk(GObj *volatile self);

/* PC port: defined below */
static ActMail ropeSwing_mes[2];

void actSt47aRopeSwingChk(GObj *volatile self);

/* PC port: defined below */
static ActMail ropeSwing_mes[2];

void actSt47aRopeSwingChk(GObj *volatile self);

/* PC port: defined below */
static ActMail ropeSwing_mes[2];

void actSt47aRopeSwingChk(GObj *volatile self);

/* PC port: defined below */
static ActMail ropeSwing_mes[2];

void actSt47aRopeSwingChk(GObj *volatile self);

/* PC port: defined below */
static ActMail ropeSwing_mes[2];

void actSt47aRopeSwingChk(GObj *volatile self);

/* PC port: defined below */
static ActMail ropeSwing_mes[2];

void actSt47aRopeSwingChk(GObj *volatile self);

/* PC port: defined below */
static ActMail ropeSwing_mes[2];

void actSt47aRopeSwingChk(GObj *volatile self);

/* PC port: defined below */
static ActMail ropeSwing_mes[2];

void actSt47aRopeSwingChk(GObj *volatile self);

/* PC port: defined below */
static ActMail ropeSwing_mes[2];

void actSt47aRopeSwingChk(GObj *volatile self);

/* PC port: defined below */
static ActMail ropeSwing_mes[2];

void actSt47aRopeSwingChk(GObj *volatile self);

/* PC port: defined below */
static ActMail ropeSwing_mes[2];

void actSt47aRopeSwingChk(GObj *volatile self);

/* PC port: defined below */
static ActMail ropeSwing_mes[2];

void actSt47aRopeSwingChk(GObj *volatile self);

inline void actSt47aRopeSwing(GObj *volatile self) /* derived name */
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(45) == 0) {
        ropeSwing_mes[0].func = actSt47aRopeSwingChk;
        act->mail = ropeSwing_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    }
}

/* .sbss: the demo's own end flag, raised by the subthread the wait loop below
   spins for. */
static int demoEnd;

static void actSt47aRopeChk_ntsc(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(46) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(439))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(148, 1, 0);
            while (stage_CheckAnimationFinish(148) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(439)->active = 0;

            lt_switch_layout(51);
            scpBoyControlReadDisable = 1;

            gflagOn(46);

            scpSleepEnemyAll();

            _ACTWait(15);

            stage_SetAnimation(149, 1, 0);

            while (stage_CheckAnimationFrame(149, 148, 0) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);

            scpSearchGobj(460)->active = 1;

            while (stage_CheckAnimationFinish(149) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);

            scpWakeupEnemyAll();
            scpBoyControlReadDisable = 0;
            lt_switch_layout(50);
            break;
        }
    }
}

static void actSt47aRopeChk_jun26(GObj *volatile self)
{
    while (1) {
        if (scpGameStat_BoyWeaponkind() == 4 && scpTriggerBall(self, boyGObj, 100.0f) != 0 &&
            (ForMotionViewer_GetCurrentMotion(boyGObj) == 39 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 44 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 46 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 40 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 41 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 62)) {
            break;
        }
        _ACTWait(1);
    }

    lt_switch_layout(51);

    gflagOn(45);

    scpBoyControlReadDisable = 1;

    iosPadActRequest(boyPad, 17);

    _ACTWait(30);

    stage_SetAnimation(135, 1, 0);

    stage_SetAnimation(134, 1, 65);

    soundSeDefPlay(1262, 0, 0, 1);

    while (stage_CheckAnimationFinish(134) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpSearchGobj(442)->active = 1;

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
}

static void actSt47aRopeChk_aug6(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(46) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(438))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(147, 1, 0);
            while (stage_CheckAnimationFinish(147) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(438)->active = 0;

            lt_switch_layout(51);
            scpBoyControlReadDisable = 1;

            gflagOn(46);

            scpSleepEnemyAll();

            _ACTWait(15);

            stage_SetAnimation(148, 1, 0);

            while (stage_CheckAnimationFrame(148, 148, 0) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);

            scpSearchGobj(459)->active = 1;

            while (stage_CheckAnimationFinish(148) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);

            scpWakeupEnemyAll();
            scpBoyControlReadDisable = 0;
            lt_switch_layout(50);
            break;
        }
    }
}

void actSt47aRopeChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt47aRopeChk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt47aRopeChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt47aRopeChk_aug6(self);
        return;
    }
    GObj *x = self;
    GProc *th;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(49) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(scpSearchGobj(482))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(PAL_JPNKORCN(169, 165), 1, 0);
            while (stage_CheckAnimationFinish(PAL_JPNKORCN(169, 165)) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(482)->active = 0;

            lt_switch_layout(PAL_JPNKORCN(55, 52));
            scpBoyControlReadDisable = 1;

            gflagOn(49);
            FinishHint(5);

            scpSleepEnemyAll();

            th = actCreateSubThread(actSt47aRopeSub, 21);

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
                stage_SetAnimation(PAL_JPNKORCN(170, 166), 0, -1);
                scpSearchGobj(503)->active = 1;
                scpFadeIn(3.0f);
            }

            scpWakeupEnemyAll();
            scpBoyControlReadDisable = 0;
            lt_switch_layout(PAL_JPNKORCN(54, 51));
            break;
        }
    }
}

static ActMail ropeSwing_mes[2] = {{377}, {376}}; /* derived name */

static void actSt47aBarricadeChk_ntsc(GObj *volatile self)
{
    GObj *n;

    while ((n = scpIsBombExplode(19)) == 0 || scpTriggerBall(self, n, 350.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    scpSleepEnemyAll();

    stage_SetAnimation(150, 1, 0);

    gflagOn(47);

    while (stage_CheckAnimationFinish(150) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);

    scpWakeupEnemyAll();
}

static inline void actSt47aBarricadeChk_aug6jun26(GObj *volatile self)
{
    GObj *n;

    while ((n = scpIsBombExplode(19)) == 0 ||
           scpTriggerBall(self, n, PAL_JUN26(350.0f, 500.0f)) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    if (!ICO_JUN26) {
        scpSleepEnemyAll();
    }

    stage_SetAnimation(PAL_JUN26(149, 136), 1, 0);

    gflagOn(PAL_JUN26(47, 46));

    while (stage_CheckAnimationFinish(PAL_JUN26(149, 136)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    if (!ICO_JUN26) {
        scpWakeupEnemyAll();
    }
}

void actSt47aBarricadeChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt47aBarricadeChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt47aBarricadeChk_aug6jun26(self);
        return;
    }
    GObj *n;

    while ((n = scpIsBombExplode(19)) == 0 || scpTriggerBall(self, n, 350.0f) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;

    FinishHint(4);
    FinishHint(6);

    scpSleepEnemyAll();

    _ACTWait(5);

    stage_SetAnimation(PAL_JPNKORCN(171, 167), 1, 0);

    gflagOn(50);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(171, 167)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 3);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));

    scpWakeupEnemyAll();
}

void actSt47aEnemy1(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    Generator_Mask(scpSearchGobj(PAL_NTSC_AUG6_JUN26(513, 470, 469, 453)));

    while (gflagChk(PAL_NTSCAUG6_JUN26(53, 50, 49)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    Generator_MaskOff(self);

    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);

    Generator_Call(scpSearchGobj(PAL_NTSC_AUG6_JUN26(513, 470, 469, 453)));
}

void actSt47aTorch(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(153, 139, 132)) != 0) {
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(489, 446, 445, 428));
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(490, 447, 446, 429));
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(491, 448, 447, 430));
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(492, 449, 448, 431));
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(493, 450, 449, 432));
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(494, 451, 450, 433));
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(495, 452, 451, 434));
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(496, 453, 452, 435));
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(497, 454, 453, 436));
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(498, 455, 454, 437));
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(499, 456, 455, 438));
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(500, 457, 456, 439));
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(501, 458, 457, 440));
        scpTorchLightOn(PAL_NTSC_AUG6_JUN26(502, 459, 458, 441));
    }
}

static void actSt47aSekizo1_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(40) == 0) {
        stage_SetAnimation(140, 0, 0);
        sekizo1_mes[0].func = actSt47aSekizo1Chk;
        act->mail = sekizo1_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        stage_SetAnimation(140, 0, -1);
        if (gflagChk(123) == 0) {
            ScpCallCameraSetTarget(-3000.0f, 272.0f, 0.0f);
        }
    }
}

static inline void actSt47aSekizo1_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_JUN26(40, 39)) == 0) {
        stage_SetAnimation(PAL_JUN26(139, 126), 0, 0);
        sekizo1_mes[0].func = actSt47aSekizo1Chk;
        act->mail = sekizo1_mes;
        ACTSendMailCorrect(self, PAL_JUN26(393, 377));
        _ACTWait(0);
    } else {
        if (ICO_JUN26) {
            stage_SetAnimation(126, 0, -1);
        } else {
            stage_SetAnimation(139, 0, -1);
            if (gflagChk(123) == 0) {
                ScpCallCameraSetTarget(-3000.0f, 272.0f, 0.0f);
            }
        }
    }
}

void actSt47aSekizo1(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt47aSekizo1_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt47aSekizo1_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(43) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(163, 159), 0, 0);
        sekizo1_mes[0].func = actSt47aSekizo1Chk;
        act->mail = sekizo1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(163, 159), 0, -1);
        if (gflagChk(137) == 0) {
            ScpCallCameraSetTarget(-3000.0f, 272.0f, 0.0f);
        }
    }
}

static void actSt47aSekizo2_ntsc(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    scpSekizou(self, 41, 141, 0, 18, -2450.0f, -1372.0f, -1150.0f, -2450.0f, -1372.0f, -1250.0f);
}

static inline void actSt47aSekizo2_aug6jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    scpSekizou(self, PAL_JUN26(41, 40), PAL_JUN26(140, 127), 0, PAL_JUN26(18, 21), -2450.0f,
               -1372.0f, -1150.0f, -2450.0f, -1372.0f, -1250.0f);
}

void actSt47aSekizo2(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt47aSekizo2_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt47aSekizo2_aug6jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    scpSekizou(self, 44, PAL_JPNKORCN(164, 160), 0, 18, -2450.0f, -1372.0f, -1150.0f, -2450.0f,
               -1372.0f, -1250.0f);
}

void actSt47aSekizo3(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    scpSekizou(self, PAL_JUN26(42, 41), PAL_AUG6_JUN26(142, 141, 128), 0, PAL_JUN26(18, 21),
               1700.0f, -1972.0f, 650.0f, 1700.0f, -1972.0f, 750.0f);
}

void actSt47aSekizo4(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    scpSekizou(self, PAL_JUN26(43, 42), PAL_AUG6_JUN26(143, 142, 129), 0, PAL_JUN26(18, 21),
               1700.0f, -1972.0f, -650.0f, 1700.0f, -1972.0f, -750.0f);
}

static void actSt47aHane1_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    gflagChk(44);

    hane1_mes[0].func = actSt47aHane1Main;
    act->mail = hane1_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt47aHane1_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    gflagChk(PAL_JUN26(44, 43));

    hane1_mes[0].func = actSt47aHane1Main;
    act->mail = hane1_mes;
    ACTSendMailCorrect(self, PAL_JUN26(393, 377));
    _ACTWait(0);
}

void actSt47aHane1(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt47aHane1_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt47aHane1_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    gflagChk(47);

    hane1_mes[0].func = actSt47aHane1Main;
    act->mail = hane1_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt47aHane2_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    hane2_mes[0].func = actSt47aHane2Main;
    act->mail = hane2_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt47aHane2_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    hane2_mes[0].func = actSt47aHane2Main;
    act->mail = hane2_mes;
    ACTSendMailCorrect(self, PAL_JUN26(393, 377));
    _ACTWait(0);
}

void actSt47aHane2(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt47aHane2_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt47aHane2_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    hane2_mes[0].func = actSt47aHane2Main;
    act->mail = hane2_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt47aBarricade_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(47) == 0) {
        stage_SetAnimation(150, 0, 0);
        barricade_mes[0].func = actSt47aBarricadeChk;
        act->mail = barricade_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        stage_SetAnimation(150, 0, -1);
    }
}

static inline void actSt47aBarricade_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_JUN26(47, 46)) == 0) {
        stage_SetAnimation(PAL_JUN26(149, 136), 0, 0);
        barricade_mes[0].func = actSt47aBarricadeChk;
        act->mail = barricade_mes;
        ACTSendMailCorrect(self, PAL_JUN26(393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JUN26(149, 136), 0, -1);
    }
}

void actSt47aBarricade(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt47aBarricade_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt47aBarricade_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(50) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(171, 167), 0, 0);
        barricade_mes[0].func = actSt47aBarricadeChk;
        act->mail = barricade_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        FinishHint(4);
        FinishHint(6);
        stage_SetAnimation(PAL_JPNKORCN(171, 167), 0, -1);
    }
}

static void actSt47aExit_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    exit_mes[0].func = actSt47aExitChk;
    act->mail = exit_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt47aExit_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    exit_mes[0].func = actSt47aExitChk;
    act->mail = exit_mes;
    ACTSendMailCorrect(self, PAL_JUN26(393, 377));
    _ACTWait(0);
}

void actSt47aExit(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt47aExit_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt47aExit_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    exit_mes[0].func = actSt47aExitChk;
    act->mail = exit_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt47aExit2_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(130) != 0) {
        exit2_mes[0].func = actSt47aExit2Chk;
        act->mail = exit2_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt47aExit2_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_JUN26(130, 123)) != 0) {
        exit2_mes[0].func = actSt47aExit2Chk;
        act->mail = exit2_mes;
        ACTSendMailCorrect(self, PAL_JUN26(393, 377));
        _ACTWait(0);
    }
}

void actSt47aExit2(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt47aExit2_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt47aExit2_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(144) != 0) {
        exit2_mes[0].func = actSt47aExit2Chk;
        act->mail = exit2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static void actSt47aEne_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(49) == 0) {
        ene_mes[0].func = actSt47aEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt47aEne_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_JUN26(49, 48)) == 0) {
        ene_mes[0].func = actSt47aEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, PAL_JUN26(393, 377));
        _ACTWait(0);
    }
}

void actSt47aEne(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt47aEne_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt47aEne_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(52) == 0) {
        ene_mes[0].func = actSt47aEneChk;
        act->mail = ene_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static void actSt47aEnemy2_ntsc(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(50) == 0) {
        _ACTWait(1);
    }
    _ACTWait(180);

    Generator_MaskOff(self);
    Generator_Call(self);

    _ACTWait(60);

    Generator_Call(self);
}

static inline void actSt47aEnemy2_aug6jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(PAL_JUN26(50, 49)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(PAL_JUN26(180, 1));

    Generator_MaskOff(self);
    Generator_Call(self);

    _ACTWait(60);

    Generator_Call(self);
}

void actSt47aEnemy2(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt47aEnemy2_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt47aEnemy2_aug6jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(53) == 0) {
        _ACTWait(1);
    }
    _ACTWait(180);

    Generator_MaskOff(self);
    Generator_Call(self);

    _ACTWait(60);

    Generator_Call(self);
}

static void actSt47aEnemy3_ntsc(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(50) == 0) {
        _ACTWait(1);
    }
    _ACTWait(180);

    Generator_MaskOff(self);
    Generator_Call(self);

    _ACTWait(60);

    Generator_Call(self);
}

static inline void actSt47aEnemy3_aug6jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(50) == 0) {
        _ACTWait(1);
    }

    _ACTWait(180);

    Generator_MaskOff(self);
    Generator_Call(self);

    _ACTWait(60);

    Generator_Call(self);
}

void actSt47aEnemy3(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt47aEnemy3_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt47aEnemy3_aug6jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(53) == 0) {
        _ACTWait(1);
    }
    _ACTWait(180);

    Generator_MaskOff(self);
    Generator_Call(self);

    _ACTWait(60);

    Generator_Call(self);
}

static void actSt47aEnemy4_ntsc(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(50) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);
}

static inline void actSt47aEnemy4_aug6jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(50) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);
}

void actSt47aEnemy4(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt47aEnemy4_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt47aEnemy4_aug6jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(53) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);
}

void actSt47aHint2On(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(54) == 0) {
        SleepHint(5);

        hint2On_mes[0].func = actSt47aHint2OnChk;
        act->mail = hint2On_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt47aSekizo1Event(int x)
{
    volatile int local = x;
}

/* A 16-byte constant vector template: the float view carries the values,
   the long long view is the one the copy reads. */

static const ConstVec girlWayPos = {{2266.0f, -272.0f, 0.0f, 0.0f}}; /* derived name */

static const ConstVec hane1_1GirlPos = {{-1076.0f, -1972.0f, 755.0f, 0.0f}}; /* derived name */

static const ConstVec hane1_2GirlPos = {{1028.0f, -1972.0f, 744.0f, 0.0f}}; /* derived name */

static const ConstVec hane2GirlPos = {{-1031.0f, -1972.0f, -747.0f, 0.0f}}; /* derived name */

static void actSt47aGirlWay_ntsc(GObj *volatile self)
{
    long long buf[2];
    long long way[2];

    buf[0] = girlWayPos.d[0];
    buf[1] = girlWayPos.d[1];
    _SCPMoveCharactorByWay(girlGObj, 0, (float *)buf, 100.0f, 0);

    memset(way, 0, 16);
    RequestStageChangeDirect(girlGObj, 11, way, 180);
    scpWakeupEnemyAll();

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
    brainUnlockGirl();
}

static inline void actSt47aGirlWay_aug6jun26(GObj *volatile self)
{
    long long buf[2];
    long long way[2];

    buf[0] = girlWayPos.d[0];
    buf[1] = girlWayPos.d[1];
    _SCPMoveCharactorByWay(girlGObj, 0, (float *)buf, 100.0f, 0);

    memset(way, 0, 16);
    RequestStageChangeDirect(girlGObj, 11, way, 180);

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
    brainUnlockGirl();
}

void actSt47aGirlWay(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt47aGirlWay_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt47aGirlWay_aug6jun26(self);
        return;
    }
    long long buf[2];
    long long way[2];

    buf[0] = girlWayPos.d[0];
    buf[1] = girlWayPos.d[1];
    _SCPMoveCharactorByWay(girlGObj, 0, (float *)buf, 100.0f, 0);

    memset(way, 0, 16);
    RequestStageChangeDirect(girlGObj, 11, way, 180);
    brainUnlockGirl();
}

void actSt47aSekizo2Event(int x)
{
    volatile int local = x;
}

void actSt47aSekizo3Event(int x)
{
    volatile int local = x;
}

void actSt47aSekizo4Event(int x)
{
    volatile int local = x;
}

static inline void actSt47aHane1Main_aug6jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    if (!ICO_JUN26) {
        scpWakeupEnemyAll();
    }

    sub->mainMail = hane1Main_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt47aHane1Main(GObj *volatile self)
{
    if (ICO_AUG6JUN26) {
        actSt47aHane1Main_aug6jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = hane1Main_mes;
    while (1) {
        _ACTWait(1);
    }
}

static void actSt47aHane1Switch_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    if (gflagChk(44) != 0) {
        hane1SwitchUp_mes[0].func = actSt47aHane1Up;
        sub->mail = hane1SwitchUp_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }

    hane1SwitchDown_mes[0].func = actSt47aHane1Down;
    sub->mail = hane1SwitchDown_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt47aHane1Switch_aug6jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    if (!ICO_JUN26) {
        scpSleepEnemyAll();
    }

    if (gflagChk(PAL_JUN26(44, 43)) != 0) {
        hane1SwitchUp_mes[0].func = actSt47aHane1Up;
        sub->mail = hane1SwitchUp_mes;
        ACTSendMailCorrect(self, PAL_JUN26(393, 377));
        _ACTWait(0);
    }

    hane1SwitchDown_mes[0].func = actSt47aHane1Down;
    sub->mail = hane1SwitchDown_mes;
    ACTSendMailCorrect(self, PAL_JUN26(393, 377));
    _ACTWait(0);
}

void actSt47aHane1Switch(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt47aHane1Switch_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt47aHane1Switch_aug6jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    if (gflagChk(47) != 0) {
        hane1SwitchUp_mes[0].func = actSt47aHane1Up;
        sub->mail = hane1SwitchUp_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }

    hane1SwitchDown_mes[0].func = actSt47aHane1Down;
    sub->mail = hane1SwitchDown_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt47aHane1_1Girl(GObj *volatile self)
{
    long long buf[2];
    buf[0] = hane1_1GirlPos.d[0];
    buf[1] = hane1_1GirlPos.d[1];
    _SCPMoveCharactorByWay(girlGObj, 0, (float *)buf, 100.0f, 0);
}

void actSt47aHane1_2Girl(GObj *volatile self)
{
    long long buf[2];
    buf[0] = hane1_2GirlPos.d[0];
    buf[1] = hane1_2GirlPos.d[1];
    _SCPMoveCharactorByWay(girlGObj, 0, (float *)buf, 100.0f, 0);
}

static inline void actSt47aHane2Main_aug6jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    if (!ICO_JUN26) {
        scpWakeupEnemyAll();
    }

    sub->mainMail = hane2Main_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt47aHane2Main(GObj *volatile self)
{
    if (ICO_AUG6JUN26) {
        actSt47aHane2Main_aug6jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = hane2Main_mes;
    while (1) {
        _ACTWait(1);
    }
}

static void actSt47aHane2Switch_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    if (gflagChk(45) != 0) {
        hane2SwitchUp_mes[0].func = actSt47aHane2Up;
        sub->mail = hane2SwitchUp_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }

    hane2SwitchDown_mes[0].func = actSt47aHane2Down;
    sub->mail = hane2SwitchDown_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt47aHane2Switch_aug6jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    if (!ICO_JUN26) {
        scpSleepEnemyAll();
    }

    if (gflagChk(PAL_JUN26(45, 44)) != 0) {
        hane2SwitchUp_mes[0].func = actSt47aHane2Up;
        sub->mail = hane2SwitchUp_mes;
        ACTSendMailCorrect(self, PAL_JUN26(393, 377));
        _ACTWait(0);
    }

    hane2SwitchDown_mes[0].func = actSt47aHane2Down;
    sub->mail = hane2SwitchDown_mes;
    ACTSendMailCorrect(self, PAL_JUN26(393, 377));
    _ACTWait(0);
}

void actSt47aHane2Switch(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt47aHane2Switch_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt47aHane2Switch_aug6jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    if (gflagChk(48) != 0) {
        hane2SwitchUp_mes[0].func = actSt47aHane2Up;
        sub->mail = hane2SwitchUp_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }

    hane2SwitchDown_mes[0].func = actSt47aHane2Down;
    sub->mail = hane2SwitchDown_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt47aHane2Girl(GObj *volatile self)
{
    long long buf[2];
    buf[0] = hane2GirlPos.d[0];
    buf[1] = hane2GirlPos.d[1];
    _SCPMoveCharactorByWay(girlGObj, 0, (float *)buf, 100.0f, 0);
}

static void actSt47aRopeSub(GObj *volatile self)
{
    _ACTWait(15);

    stage_SetAnimation(PAL_JPNKORCN(170, 166), 1, 0);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(170, 166), 148, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpSearchGobj(503)->active = 1;

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(170, 166)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

void actSt47aBarricadeEvent(int x)
{
    volatile int local = x;
}

void actSt47aRopeSwingChk(GObj *volatile self);         /* derived name */
inline void actSt47aRopeSwingDemo(GObj *volatile self); /* derived name */

/* The rope swings when the boy strikes it without the sword: the check
   threads the swing and the swing re-arms the check. */
inline void actSt47aRopeSwingChk(GObj *volatile self) /* derived name */
{
    while (1) {
        if (scpGameStat_BoyWeaponkind() != 4 && scpTriggerBall(self, boyGObj, 100.0f) != 0 &&
            (ForMotionViewer_GetCurrentMotion(boyGObj) == 39 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 44 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 46 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 40 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 41 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 62)) {
            break;
        }
        _ACTWait(1);
    }

    actCreateSubThread(actSt47aRopeSwingDemo, 21);
}

inline void actSt47aRopeSwingDemo(GObj *volatile self) /* derived name */
{
    Act *act = GOBJ_ACT(self);
    static ActMail ropeSwingDemo_mes[2] = {{377}, {376}}; /* derived name */

    stage_SetAnimation(134, 1, 0);

    soundSeDefPlay(1236, 0, 0, 1);

    while (stage_CheckAnimationFrame(134, 63, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    ropeSwingDemo_mes[0].func = actSt47aRopeSwingChk;
    act->mail = ropeSwingDemo_mes;
    ACTSendMailCorrect(self, 377);
    _ACTWait(0);
}

inline void actSt47aRopeSwingEvent(int x) /* derived name */
{
    volatile int local = x;
}

static void actSt47aExitChk_ntsc(GObj *volatile self)
{
    scpSearchGobj(438)->active = 0;

    while ((gflagChk(155) == 0) || (gflagChk(209) == 0)) {
        _ACTWait(1);
    }

    gflagOn(48);
    scpSearchGobj(437)->active = 0;
    scpSearchGobj(438)->active = 1;
}

static inline void actSt47aExitChk_jun26(GObj *volatile self)
{
    scpSearchGobj(420)->active = 0;

    while ((gflagChk(148) == 0) || (gflagChk(200) == 0)) {
        _ACTWait(1);
    }

    gflagOn(47);
    scpSearchGobj(419)->active = 0;
    scpSearchGobj(420)->active = 1;
}

static inline void actSt47aExitChk_aug6(GObj *volatile self)
{
    scpSearchGobj(437)->active = 0;

    while ((gflagChk(155) == 0) || (gflagChk(209) == 0)) {
        _ACTWait(1);
    }

    gflagOn(48);
    scpSearchGobj(436)->active = 0;
    scpSearchGobj(437)->active = 1;
}

void actSt47aExitChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt47aExitChk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt47aExitChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt47aExitChk_aug6(self);
        return;
    }
    scpSearchGobj(481)->active = 0;

    while ((gflagChk(174) == 0) || (gflagChk(234) == 0)) {
        _ACTWait(1);
    }

    gflagOn(51);
    scpSearchGobj(480)->active = 0;
    scpSearchGobj(481)->active = 1;
}

static void actSt47aExit2Chk_ntsc(GObj *volatile self)
{
    scpSearchGobj(437)->active = 1;
    scpSearchGobj(438)->active = 0;
}

static inline void actSt47aExit2Chk_jun26(GObj *volatile self)
{
    scpSearchGobj(419)->active = 1;
    scpSearchGobj(420)->active = 0;
}

static inline void actSt47aExit2Chk_aug6(GObj *volatile self)
{
    scpSearchGobj(436)->active = 1;
    scpSearchGobj(437)->active = 0;
}

void actSt47aExit2Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt47aExit2Chk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt47aExit2Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt47aExit2Chk_aug6(self);
        return;
    }
    scpSearchGobj(480)->active = 1;
    scpSearchGobj(481)->active = 0;
}

static void actSt47aEneChk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    gflagOff(358);
    _ACTWait(1);
    gflagOn(49);
    gflagOn(50);
}

static inline void actSt47aEneChk_jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    gflagOn(48);
    gflagOn(49);
}

static inline void actSt47aEneChk_aug6(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    gflagOff(357);
    _ACTWait(1);
    gflagOn(49);
    gflagOn(50);
}

void actSt47aEneChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt47aEneChk_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt47aEneChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt47aEneChk_aug6(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    gflagOff(391);
    _ACTWait(1);
    gflagOn(52);
    gflagOn(53);
}

static void actSt47aHint2OnChk(GObj *volatile self)
{
    while (gflagChk(47) == 0) {
        _ACTWait(1);
    }

    gflagOn(54);
    WakeupHint(5);
}
