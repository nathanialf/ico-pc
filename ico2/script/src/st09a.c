#include "st09a.h"
#include "debug.h"
#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
#include "s_init.h"
#include "act.h"
#include "gobj_process.h"
#include "commonact.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "script.h"
#include "StageAnimation.h"
#include "motionManager2.h"
#include "typedef.h"
#include "main.h"

static void actSt09aBrgDownSub(GObj *volatile self);
static void actSt09aHint1Chk(GObj *volatile self);
static void actSt09aHint2Chk(GObj *volatile self);

/* .sbss: the demo's own end flag, raised by the subthread the wait loop below
   spins for. */
static int demoEnd;

/* .data: actor mail packets. */

static ActMail elvMain_mes_pal[2] = {{406, actSt09aElvSwitch}, {429}}; /* derived name */

static ActMail elvMain_mes_ntsc[2] = {{376, actSt09aElvSwitch}, {396}}; /* derived name */

static ActMail elvMain_mes_aug6[2] = {{372, actSt09aElvSwitch}, {392}}; /* derived name */

static ActMail elvMain_mes_jun26[2] = {{356, actSt09aElvSwitch}, {376}}; /* derived name */

#define elvMain_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(elvMain_mes_pal, elvMain_mes_ntsc, elvMain_mes_aug6, elvMain_mes_jun26)

static ActMail elv_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail elv_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail elv_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail elv_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define elv_mes PAL_NTSC_AUG6_JUN26(elv_mes_pal, elv_mes_ntsc, elv_mes_aug6, elv_mes_jun26)

static ActMail elvSwitchUp_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail elvSwitchUp_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail elvSwitchUp_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail elvSwitchUp_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define elvSwitchUp_mes                                                                            \
    PAL_NTSC_AUG6_JUN26(elvSwitchUp_mes_pal, elvSwitchUp_mes_ntsc, elvSwitchUp_mes_aug6,           \
                        elvSwitchUp_mes_jun26)

static ActMail elvSwitchDown_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail elvSwitchDown_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail elvSwitchDown_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail elvSwitchDown_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define elvSwitchDown_mes                                                                          \
    PAL_NTSC_AUG6_JUN26(elvSwitchDown_mes_pal, elvSwitchDown_mes_ntsc, elvSwitchDown_mes_aug6,     \
                        elvSwitchDown_mes_jun26)

static ActMail elvDown_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail elvDown_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail elvDown_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail elvDown_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define elvDown_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(elvDown_mes_pal, elvDown_mes_ntsc, elvDown_mes_aug6, elvDown_mes_jun26)

static ActMail elvUp_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail elvUp_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail elvUp_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail elvUp_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define elvUp_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(elvUp_mes_pal, elvUp_mes_ntsc, elvUp_mes_aug6, elvUp_mes_jun26)

static float sekizoPos[4] = {1548.0f, -412.0f, -608.0f, 0.0f}; /* derived name */

static ActMail intro_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail intro_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail intro_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail intro_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define intro_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(intro_mes_pal, intro_mes_ntsc, intro_mes_aug6, intro_mes_jun26)

static ActMail brgMain_mes_pal[2] = {{407, actSt09aBrgSwitch}, {429}}; /* derived name */

static ActMail brgMain_mes_ntsc[2] = {{377, actSt09aBrgSwitch}, {396}}; /* derived name */

static ActMail brgMain_mes_aug6[2] = {{373, actSt09aBrgSwitch}, {392}}; /* derived name */

static ActMail brgMain_mes_jun26[2] = {{357, actSt09aBrgSwitch}, {376}}; /* derived name */

#define brgMain_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(brgMain_mes_pal, brgMain_mes_ntsc, brgMain_mes_aug6, brgMain_mes_jun26)

static ActMail brg_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail brg_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail brg_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail brg_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define brg_mes PAL_NTSC_AUG6_JUN26(brg_mes_pal, brg_mes_ntsc, brg_mes_aug6, brg_mes_jun26)

static ActMail brgSwitch_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail brgSwitch_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail brgSwitch_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail brgSwitch_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define brgSwitch_mes                                                                              \
    PAL_NTSC_AUG6_JUN26(brgSwitch_mes_pal, brgSwitch_mes_ntsc, brgSwitch_mes_aug6,                 \
                        brgSwitch_mes_jun26)

static ActMail hint1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail hint2_mes[2] = {{430}, {429}}; /* derived name */

/* .sdata: the bridge stream handle. */
SqEntry *st09a_brg = 0;

void actSt09aInit(void)
{
    if (gflagChk(PAL_NTSCAUG6_JUN26(86, 79, 77)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(378, 374, 343, 342, 309), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(378, 374, 343, 342, 309), 0, -1);
    }
}

void actSt09aElvDown(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);
    /* the sound handle, which the sound subsystem owns; it is kept across
       the animation wait and read again for soundSeDefStop */
    volatile int se;

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
    LockForceGroundParent(boyGObj);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(375, 371, 340, 339, 306), 1, 0);

    se = soundSeDefPlay(PAL_NTSCAUG6_JUN26(1217, 1193, 1167), 0, 0, 1);

    gflagOn(PAL_NTSCAUG6_JUN26(83, 76, 74));

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6_JUN26(375, 371, 340, 339, 306), 210,
                                     1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    soundSeDefStop(se);

    UnlockForceGroundParent(boyGObj);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));

    elvDown_mes[0].func = actSt09aElvMain;
    act->mail = elvDown_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

static void actSt09aBrgDown_ntsc(GObj *volatile self)
{
    lt_switch_layout(51);
    gflagOn(79);
    _ACTWait(60);
    scpAdpcmPlayRequestFunc(88, &st09a_brg, 1, 1, 1);

    while (st09a_brg == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(343, 1, 0);

    while (stage_CheckAnimationFrame(343, 115, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 17);

    while (stage_CheckAnimationFrame(343, 165, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFinish(343) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static void actSt09aBrgDown_aug6jun26(GObj *volatile self)
{
    lt_switch_layout(51);
    gflagOn(PAL_JUN26(79, 77));
    _ACTWait(60);
    scpAdpcmPlayRequestFunc(PAL_JUN26(88, 83), &st09a_brg, 1, 1, 1);

    while (st09a_brg == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JUN26(342, 309), 1, 0);

    while (stage_CheckAnimationFrame(PAL_JUN26(342, 309), 115, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 17);

    while (stage_CheckAnimationFrame(PAL_JUN26(342, 309), 165, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFinish(PAL_JUN26(342, 309)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actSt09aBrgDown(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt09aBrgDown_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt09aBrgDown_aug6jun26(self);
        return;
    }
    GProc *th;

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    gflagOn(86);
    scpAdpcmPlayRequestFunc(90, &st09a_brg, 1, 1, 1);

    while (st09a_brg == 0) {
        _ACTWait(1);
    }

    th = actCreateSubThread(actSt09aBrgDownSub, 21);
    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&th->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);
        scpAdpcmFadeCloseFunc(&st09a_brg, 256);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }

        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(PAL_JPNKORCN(378, 374), 0, -1);
        scpFadeIn(5.0f);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    }
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

void actSt09aElv(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(83, 76, 74)) != 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(375, 371, 340, 339, 306), 0, 210);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(375, 371, 340, 339, 306), 0, 0);
    }

    elv_mes[0].func = actSt09aElvMain;
    act->mail = elv_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

static void actSt09aSekizo_jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    /* the 2001 builds pass a copy of the same point */
    soundSeDefPlay(1290, 0, sekizoPos, 1);
    soundSeDefPlay(1291, 0, sekizoPos, 1);
    soundSeDefPlay(1292, 0, sekizoPos, 1);

    scpSekizou(self, 75, 307, 0, 21, -1350.0f, -100.0f, 1515.0f, -1450.0f, -100.0f, 1515.0f);
}

void actSt09aSekizo(GObj *volatile self)
{
    if (ICO_JUN26) {
        actSt09aSekizo_jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    /* the 2001 builds pass a copy of the same point */
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1346, 1319), 0, sekizoPos, 1);
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1347, 1320), 0, sekizoPos, 1);
    soundSeDefPlay(PAL_NTSCAUG6JUN26(1348, 1321), 0, sekizoPos, 1);

    if (ICO_NTSC) {
        scpSekizou(self, 77, 341, 0, 18, -1350.0f, -100.0f, 1515.0f, -1450.0f, -100.0f, 1515.0f);
    } else if (ICO_AUG6JUN26) {
        scpSekizou(self, 77, 340, 0, 18, -1350.0f, -100.0f, 1515.0f, -1450.0f, -100.0f, 1515.0f);
    } else {
        scpSekizou(self, 84, PAL_JPNKORCN(376, 372), 0, 18, -1350.0f, -100.0f, 1515.0f, -1450.0f,
                   -100.0f, 1515.0f);
    }
}

void actSt09aIntro(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(85, 78, 76)) == 0) {
        intro_mes[0].func = actSt09aIntroChk;
        act->mail = intro_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }
}

void actSt09aBrg(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6_JUN26(86, 79, 77)) == 0) {
        brg_mes[0].func = actSt09aBrgMain;
        act->mail = brg_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }
}

void actSt09aHint1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(87) == 0) {
        hint1_mes[0].func = actSt09aHint1Chk;
        act->mail = hint1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        FinishHint(10);
    }
}

void actSt09aHint2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(88) == 0) {
        hint2_mes[0].func = actSt09aHint2Chk;
        act->mail = hint2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        FinishHint(11);
    }
}

void actSt09aElvMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 0;

    sub->mainMail = elvMain_mes;

    while (1) {
        _ACTWait(1);
    }
}

void actSt09aElvSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    if (gflagChk(PAL_NTSCAUG6_JUN26(83, 76, 74)) != 0) {
        elvSwitchUp_mes[0].func = actSt09aElvUp;
        sub->mail = elvSwitchUp_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }

    elvSwitchDown_mes[0].func = actSt09aElvDown;
    sub->mail = elvSwitchDown_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

void actSt09aElvUp(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);
    /* the sound handle, which the sound subsystem owns; it is kept across
       the animation wait and read again for soundSeDefStop */
    volatile int se;

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(375, 371, 340, 339, 306), 1, 211);

    se = soundSeDefPlay(PAL_NTSCAUG6_JUN26(1217, 1193, 1167), 0, 0, 1);

    gflagOff(PAL_NTSCAUG6_JUN26(83, 76, 74));

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6_JUN26(375, 371, 340, 339, 306), 420,
                                     1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    soundSeDefStop(se);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));

    elvUp_mes[0].func = actSt09aElvMain;
    act->mail = elvUp_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

void actSt09aSekizoEvent(int x)
{
    volatile int local = x;
}

void actSt09aIntroChk(GObj *volatile self)
{
    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
    gflagOn(PAL_NTSCAUG6_JUN26(85, 78, 76));

    scpBoyControlReadDisable = 1;
    _ACTWait(1);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(377, 373, 342, 341, 308), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6_JUN26(377, 373, 342, 341, 308)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));

    scpBoyControlReadDisable = 0;
}

void actSt09aBrgMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    if (ICO_AUG6JUN26) {
        scpBoyControlReadDisable = 0;
    }

    sub->mainMail = brgMain_mes;

    while (1) {
        _ACTWait(1);
    }
}

void actSt09aBrgSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    brgSwitch_mes[0].func = actSt09aBrgDown;
    sub->mail = brgSwitch_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

static void actSt09aBrgDownSub(GObj *volatile self)
{
    stage_SetAnimation(PAL_JPNKORCN(378, 374), 1, 0);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(378, 374), 115, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 17);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(378, 374), 165, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(378, 374)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

static void actSt09aHint1Chk(GObj *volatile self)
{
    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    debug_StdPrintfDummy("HINT1_FINISH!!!!!!!!!!!!!!!\n");

    gflagOn(87);
    FinishHint(10);
}

static void actSt09aHint2Chk(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 400.0f) == 0) {
        _ACTWait(1);
    }

    debug_StdPrintfDummy("HINT2_FINISH!!!!!!!!!!!!!!!\n");

    gflagOn(88);
    FinishHint(11);
}
