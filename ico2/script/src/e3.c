#include "StageManager.h"
#include "debug.h"
#include "gamesys.h"
#include "layout_texture.h"
#include "sceneManager.h"
#include "pad.h"
#include "thread.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "jimaku.h"
#include "way_llf.h"
#include "brain.h"
#include "generator.h"
#include "camera-root.h"
#include "fightSound.h"
#include "gflag.h"
#include "RegistPacket.h"
#include "StageAnimation.h"
#include "motionManager2.h"
#include "staticBlur.h"
#include "windManager.h"
#include <libvu0.h>
#include <string.h>
#include "act.h"
#include "gobj_process.h"
#include "typedef.h"
#include "commonact.h"
#include "streamMotionManager.h"
#include "kanbanBoot.h"
#include "layout_action.h"
#include "script.h"
#include "main.h"

static void actE3WarningChk_jun26(GObj *volatile self)
{
    GObj *x = self;
    int i;

    actInitialize(self);
    _ACTWait(1);

    stgmgrNextStagePreLoadForceStageSet(92);

    i = 0;
    while (i++ < (60 - systemStatus[0] * 10) / systemStatus[1] * 5) {
        _ACTWait(1);
    }

    scpFadeOut(6.0f, 0, 0, 0);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    RequestStageChange(1, boyGObj, 0, 255.0f, 16.0f);
}

void actE3WarningChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3WarningChk_jun26(self);
        return;
    }
    GObj *x = self;
    int i;

    actInitialize(self);
    _ACTWait(1);

    stgmgrNextStagePreLoadForceStageSet(PAL_JPNKORNTSCAUG6JUN26CN(95, 93));

    i = 0;
    while (i++ < (60 - systemStatus[0] * 10) / systemStatus[1] * 5) {
        _ACTWait(1);
    }

    scpFadeOut(6.0f, 0, 0, 0);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    _ACTWait((int)(float)((60 - systemStatus[0] * 10) / systemStatus[1]));

    RequestStageChange(1, boyGObj, 0, 255.0f, 0.0f);
}

#include "e3.h"

static inline void actE3St09aBrgSwitch(GObj *volatile self);

/* .sdata: the title stream's handle, the capsule's, the cage fall's, the
   first gate's, the stone statue's and its volume word, then the statue
   shake's volume. */
static SqEntry *e3title = 0; /* derived name */

SqEntry *e3capsule = 0;

static SqEntry *e3cage = 0; /* derived name */

SqEntry *e3gate1st = 0;

SqEntry *sekizo_e3 = 0;

int sekizo_e3_vol = 0;

/* the 2001 builds keep the pad-shake handle and its volume in these two (sekizo_e3 and
   sekizo_e3_vol there; the European program keeps the handle in sekizo_e3_vol) */
int sekizo_e3_ntscaug6jun26 = 0;

unsigned char sekizo_e3_vol_ntscaug6jun26 = 0;

static unsigned char e3sekizo_yure_vol = 0; /* derived name */

static ActMail warning_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail warning_mes_jun26[2] = {{377}, {376}}; /* derived name */

static ActMail warning_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail warning_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define warning_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(warning_mes_pal, warning_mes_ntsc, warning_mes_aug6, warning_mes_jun26)

static ActMail title_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail title_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail title_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail title_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define title_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(title_mes_pal, title_mes_ntsc, title_mes_aug6, title_mes_jun26)

static ActMail inst1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail inst1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail inst1_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail inst1_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define inst1_mes                                                                                  \
    PAL_NTSC_AUG6_JUN26(inst1_mes_pal, inst1_mes_ntsc, inst1_mes_aug6, inst1_mes_jun26)

static ActMail capsule_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail capsule_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail capsule_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail capsule_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define capsule_mes                                                                                \
    PAL_NTSC_AUG6_JUN26(capsule_mes_pal, capsule_mes_ntsc, capsule_mes_aug6, capsule_mes_jun26)

static ActMail doorMain_mes_pal[2] = {{406, actE3DoorSwitch}, {429}}; /* derived name */

static ActMail doorMain_mes_ntsc[2] = {{376, actE3DoorSwitch}, {396}}; /* derived name */

static ActMail doorMain_mes_aug6[2] = {{372, actE3DoorSwitch}, {392}}; /* derived name */

static ActMail doorMain_mes_jun26[2] = {{356, actE3DoorSwitch}, {376}}; /* derived name */

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

static float doorUpPos[4] = {-3434.0f, -200.0f, 0.0f, 0.0f}; /* derived name */

static ActMail st13cIntro_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail st13cIntro_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail st13cIntro_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail st13cIntro_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define st13cIntro_mes                                                                             \
    PAL_NTSC_AUG6_JUN26(st13cIntro_mes_pal, st13cIntro_mes_ntsc, st13cIntro_mes_aug6,              \
                        st13cIntro_mes_jun26)

static ActMail cageFallReady_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail cageFallReady_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail cageFallReady_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail cageFallReady_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define cageFallReady_mes                                                                          \
    PAL_NTSC_AUG6_JUN26(cageFallReady_mes_pal, cageFallReady_mes_ntsc, cageFallReady_mes_aug6,     \
                        cageFallReady_mes_jun26)

static ActMail cageFall_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail cageFall_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail cageFall_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail cageFall_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define cageFall_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(cageFall_mes_pal, cageFall_mes_ntsc, cageFall_mes_aug6, cageFall_mes_jun26)

static ActMail st01bEne_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail st01bEne_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail st01bEne_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail st01bEne_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define st01bEne_mes                                                                               \
    PAL_NTSC_AUG6_JUN26(st01bEne_mes_pal, st01bEne_mes_ntsc, st01bEne_mes_aug6, st01bEne_mes_jun26)

static float st09aSekizoPos[4] = {1548.0f, -412.0f, -608.0f, 0.0f}; /* derived name */

/* main.c's: wakes the boot thread to hand the disc back to the menu (the
   stage scripts that include main.h keep a static flag of this name) */
void ico_main_demo_end(void); /* PC port: main.c's demoEnd (the flag below takes the name) */

static ActMail st09aSekizo_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail st09aSekizo_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail st09aSekizo_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail st09aSekizo_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define st09aSekizo_mes                                                                            \
    PAL_NTSC_AUG6_JUN26(st09aSekizo_mes_pal, st09aSekizo_mes_ntsc, st09aSekizo_mes_aug6,           \
                        st09aSekizo_mes_jun26)

static ActMail gate_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gate_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gate_mes_aug6[2] = {{393}, {392}}; /* derived name */

static ActMail gate_mes_jun26[2] = {{377}, {376}}; /* derived name */

#define gate_mes PAL_NTSC_AUG6_JUN26(gate_mes_pal, gate_mes_ntsc, gate_mes_aug6, gate_mes_jun26)

static ActMail st09aBrgMain_mes[2] = {{407, actE3St09aBrgSwitch}, {429}}; /* derived name */

static ActMail st09aBrg_mes[2] = {{430}, {429}}; /* derived name */

static ActMail st09aBrgSwitch_mes[2] = {{430}, {429}}; /* derived name */

static void actE3Title_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *sub;

    if (InterStageSwitchLock != 0) {
        ico_main_demo_end();
        _ACTWait(0);
    }

    sub = (Act *)actInitialize(self);

    _ACTWait(1);

    enable_game_pause = 0;
    scpBoyControlReadDisable = 1;

    e3title = 0;
    scpAdpcmPlayRequestFunc(8, &e3title, 0, 1, 0);

    while (kanbanBootEnd == 0) {
        _ACTWait(1);
    }

    systemStatus[11] = 7;

    scpFadeOut(255.0f, 0, 0, 0);

    while (e3title == 0) {
        _ACTWait(1);
    }

    scpFadeIn(6.0f);

    if ((ICO_WORD)boyGObj != 0) {
        scpPlayMot(boyGObj, 0);
    }

    ACTGame_StageChangeGObjID(47, 1, 39);
    ACTGame_StageChangeGObjID(124, 2, 40);

    stage_SetAnimation(27, 0, 0);
    stage_SetAnimation(21, 0, -1);
    stage_SetAnimation(30, 0, 0);
    stage_SetAnimation(28, 0, 0);
    stage_SetAnimation(31, 0, 0);
    stage_SetAnimation(33, 0, 0);

    title_mes[0].func = actE3TitleChk;
    sub->mail = title_mes;
    ACTSendMailCorrect(self, 377);
    _ACTWait(0);
}

void actE3Title(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3Title_jun26(self);
        return;
    }
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    enable_game_pause = 0;
    scpBoyControlReadDisable = 1;

    gflagOff(PAL_NTSC_AUG6JUN26(356, 324, 323));

    e3title = 0;
    scpAdpcmPlayRequestFunc(5, &e3title, 0, 1, 0);

    while (kanbanBootEnd == 0) {
        _ACTWait(1);
    }

    systemStatus[11] = 7;

    scpFadeOut(255.0f, 0, 0, 0);

    while (e3title == 0) {
        _ACTWait(1);
    }

    scpFadeIn(6.0f);

    if ((ICO_WORD)boyGObj != 0) {
        scpPlayMot(boyGObj, 0);
    }

    if (ICO_NTSCAUG6JUN26) {
        stage_SetAnimation(31, 0, 0);
        stage_SetAnimation(25, 0, -1);
        stage_SetAnimation(34, 0, 0);
        stage_SetAnimation(32, 0, 0);
        stage_SetAnimation(35, 0, 0);
        stage_SetAnimation(37, 0, 0);
    } else {
        stage_SetAnimation(32, 0, 0);
        stage_SetAnimation(26, 0, -1);
        stage_SetAnimation(35, 0, 0);
        stage_SetAnimation(33, 0, 0);
        stage_SetAnimation(36, 0, 0);
        stage_SetAnimation(38, 0, 0);
        stage_SetAnimation(43, 0, 0);
    }

    title_mes[0].func = actE3TitleChk;
    sub->mail = title_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

static void actE3TitleChk_jun26(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    actCreateSubThread(actE3TitleFrameChk, 21);

    while (((pad[0].flags & 0x840) == 0)) {
        _ACTWait(1);
    }

    gflagOn(309);

    AdpcmPlay(e3title->stream);

    scpFadeOut(4.0f, 0, 0, 0);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    RequestStageChange(1, boyGObj, 0, 255.0f, 16.0f);
}

void actE3TitleChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3TitleChk_jun26(self);
        return;
    }
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    actCreateSubThread(actE3TitleFrameChk, 21);

    while (PAL_JPNKORCN_NTSC_AUG6JUN26(gflagChk(356) != 0 || (pad[0].flags & 0x840) == 0,
                                       gflagChk(356) != 0 || (pad[0].flags & 0x820) == 0,
                                       gflagChk(324) != 0 || (pad[0].flags & 0x840) == 0,
                                       gflagChk(323) != 0 || (pad[0].flags & 0x840) == 0)) {
        _ACTWait(1);
    }

    gflagOn(PAL_NTSC_AUG6JUN26(357, 325, 324));
    debug_StdPrintfDummy("game_start\n");

    AdpcmPlay(e3title->stream);

    scpFadeOut(4.0f, 0, 0, 0);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    RequestStageChange(1, boyGObj, 0, 255.0f, 16.0f);
}

/* the Japanese disc's title frame: no per-language logo animation, and its own stage
   numbers */
static void actE3TitleFrameChk_jpn(GObj *volatile self)
{
    int i;

    stage_SetAnimation(931, 1, 0);

    while (stage_ContinueAnimation(931, 932) == 0) {
        _ACTWait(1);
    }

    stage_SetLoopFlag(932, 1);

    i = 0;
    while (i++ < (60 - systemStatus[0] * 10) / systemStatus[1] * 45) {
        _ACTWait(1);
    }

    stage_SetAnimation(933, 1, 0);

    while (stage_CheckAnimationFinish(933) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (gflagChk(357) == 0) {
        debug_StdPrintfDummy("mpeg_start\n");

        gflagOn(356);

        if (e3title != 0) {
            scpAdpcmCloseFunc(&e3title);
        }

        mpegPlayReturnStage = 93;
        stage_after_skipping_demo = 93;

        stgmgrForceSwitchWithFade(101, 4.0f, 4.0f);
    }
}

static void actE3TitleFrameChk_ntsc(GObj *volatile self)
{
    int i;

    stage_SetAnimation(797, 1, 0);

    while (stage_ContinueAnimation(797, 798) == 0) {
        _ACTWait(1);
    }

    stage_SetLoopFlag(798, 1);

    i = 0;
    while (i++ < (60 - systemStatus[0] * 10) / systemStatus[1] * 45) {
        _ACTWait(1);
    }

    stage_SetAnimation(799, 1, 0);

    while (stage_CheckAnimationFinish(799) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (gflagChk(325) == 0) {
        debug_StdPrintfDummy("mpeg_start\n");

        gflagOn(324);

        if (e3title != 0) {
            soundDataClose(e3title);
        }

        mpegPlayReturnStage = 93;
        stage_after_skipping_demo = 93;

        stgmgrForceSwitchWithFade(101, 4.0f, 4.0f);
    }
}

static void actE3TitleFrameChk_aug6jun26(GObj *volatile self)
{
    int i;

    stage_SetAnimation(PAL_JUN26(796, 745), 1, 0);

    while (stage_ContinueAnimation(PAL_JUN26(796, 745), PAL_JUN26(797, 746)) == 0) {
        _ACTWait(1);
    }

    stage_SetLoopFlag(PAL_JUN26(797, 746), 1);

    i = 0;
    while (i++ < (60 - systemStatus[0] * 10) / systemStatus[1] * 45) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JUN26(798, 747), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JUN26(798, 747)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (gflagChk(PAL_JUN26(324, 309)) == 0) {
        debug_StdPrintfDummy("mpeg_start\n");

        if (!ICO_JUN26) {
            gflagOn(323);
        }

        if (e3title != 0) {
            soundDataClose(e3title);
        }

        mpegPlayReturnStage = PAL_JUN26(93, 92);
        stage_after_skipping_demo = PAL_JUN26(93, 92);

        stgmgrForceSwitchWithFade(PAL_JUN26(101, 100), 4.0f, 4.0f);
    }
}

void actE3TitleFrameChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actE3TitleFrameChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actE3TitleFrameChk_aug6jun26(self);
        return;
    }
    int intro = 943;
    int loop = 938;
    int outro = 948;
    int i;

    if (ICO_JPNKORCN) {
        actE3TitleFrameChk_jpn(self);
        return;
    }

    stage_SetAnimation(935, 1, 0);

    switch (NonLinearCameraMove) {
    case 3:
        intro = 944;
        break;
    case 4:
        intro = 945;
        break;
    case 6:
        intro = 947;
        break;
    case 5:
        intro = 946;
        break;
    }
    stage_SetAnimation(intro, 1, 0);

    while (stage_ContinueAnimation(935, 936) == 0) {
        _ACTWait(1);
    }

    stage_SetLoopFlag(936, 1);

    switch (NonLinearCameraMove) {
    case 3:
        loop = 939;
        break;
    case 4:
        loop = 940;
        break;
    case 6:
        loop = 942;
        break;
    case 5:
        loop = 941;
        break;
    }
    stage_SetLoopFlag(loop, 1);
    stage_SetAnimation(loop, 1, 0);

    i = 0;
    while (i++ < (60 - systemStatus[0] * 10) / systemStatus[1] * 45) {
        _ACTWait(1);
    }

    stage_SetAnimation(937, 1, 0);

    switch (NonLinearCameraMove) {
    case 3:
        outro = 949;
        break;
    case 4:
        outro = 950;
        break;
    case 6:
        outro = 952;
        break;
    case 5:
        outro = 951;
        break;
    }
    stage_SetAnimation(outro, 1, 0);

    while (stage_CheckAnimationFinish(937) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (gflagChk(357) == 0) {
        debug_StdPrintfDummy("mpeg_start\n");

        gflagOn(356);

        if (e3title != 0) {
            scpAdpcmCloseFunc(&e3title);
        }

        mpegPlayReturnStage = 95;
        stage_after_skipping_demo = 95;

        stgmgrForceSwitchWithFade(103, 4.0f, 4.0f);
    }
}

/* the Japanese disc's instruction screen: no per-language animation, and it ends on pad bits
   0x820 */
static void actE3Inst1Chk_jpn(GObj *volatile self)
{
    GObj *x = self;
    int i;

    actInitialize(self);
    _ACTWait(1);

    i = 0;
    while (i++ < (60 - systemStatus[0] * 10) / systemStatus[1] * 10 &&
           (pad[0].flags & 0x820) == 0) {
        _ACTWait(1);
    }

    soundSeDefPlay(1390, 0, 0, 1);

    scpFadeOut(3.0f, 0, 0, 0);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    RequestStageChange(1, boyGObj, 0, 255.0f, 16.0f);
}

static void actE3Inst1Chk_jun26(GObj *volatile self)
{
    GObj *x = self;
    int i;

    actInitialize(self);
    _ACTWait(1);

    i = 0;
    while (i++ < (60 - systemStatus[0] * 10) / systemStatus[1] * 10 &&
           (pad[0].flags & 0x840) == 0) {
        _ACTWait(1);
    }

    soundSeDefPlay(1302, 0, 0, 1);

    scpFadeOut(3.0f, 0, 0, 0);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    RequestStageChange(1, boyGObj, 0, 255.0f, 16.0f);
}

static void actE3Inst1Chk_ntscaug6(GObj *volatile self)
{
    GObj *x = self;
    int i;

    if (ICO_JPNKORCN) {
        actE3Inst1Chk_jpn(self);
        return;
    }

    actInitialize(self);
    _ACTWait(1);

    i = 0;
    while (i++ < (60 - systemStatus[0] * 10) / systemStatus[1] * 10 &&
           (pad[0].flags & 0x840) == 0) {
        _ACTWait(1);
    }

    soundSeDefPlay(1353, 0, 0, 1);

    scpFadeOut(3.0f, 0, 0, 0);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    RequestStageChange(1, boyGObj, 0, 255.0f, 16.0f);
}

void actE3Inst1Chk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3Inst1Chk_jun26(self);
        return;
    }
    if (ICO_NTSCAUG6) {
        actE3Inst1Chk_ntscaug6(self);
        return;
    }
    GObj *x = self;
    int anim;
    int i;

    if (ICO_JPNKORCN) {
        actE3Inst1Chk_jpn(self);
        return;
    }

    actInitialize(self);
    _ACTWait(1);

    anim = 960;
    switch (NonLinearCameraMove) {
    case 3:
        anim = 961;
        break;
    case 4:
        anim = 962;
        break;
    case 6:
        anim = 964;
        break;
    case 5:
        anim = 963;
        break;
    }

    stage_SetLoopFlag(anim, 1);
    stage_SetAnimation(anim, 1, 0);

    i = 0;
    while (i++ < (60 - systemStatus[0] * 10) / systemStatus[1] * 10 &&
           (pad[0].flags & 0x840) == 0) {
        _ACTWait(1);
    }

    soundSeDefPlay(1390, 0, 0, 1);

    scpFadeOut(3.0f, 0, 0, 0);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    RequestStageChange(1, boyGObj, 0, 255.0f, 16.0f);
}

static void actE3Capsule_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (gflagChk(310) == 0) {
        if ((ICO_WORD)boyGObj != 0) {
            scpPlayMot(boyGObj, 0);
        }

        scpFadeOut(255.0f, 0, 0, 0);

        lt_switch_layout(51);

        stage_SetAnimation(27, 0, 0);
        stage_SetAnimation(21, 0, -1);
        stage_SetAnimation(30, 0, 0);
        stage_SetAnimation(28, 0, 0);
        stage_SetAnimation(31, 0, 0);
        stage_SetAnimation(33, 0, 0);

        capsule_mes[0].func = actE3CapsuleChk;
        sub->mail = capsule_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    } else {
        stage_SetAnimation(27, 0, -1);
        stage_SetAnimation(21, 0, -1);
        stage_SetAnimation(30, 0, 0);
        stage_SetAnimation(28, 0, 0);
        stage_SetAnimation(31, 0, 0);
        stage_SetAnimation(33, 0, 0);
    }
}

static void actE3Capsule_aug6(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if ((ICO_WORD)girlGObj == 0) {
        ScpCallCameraOff();
    }

    if (gflagChk(325) == 0) {
        if ((ICO_WORD)boyGObj != 0) {
            scpPlayMot(boyGObj, 0);
        }

        scpFadeOut(255.0f, 0, 0, 0);

        lt_switch_layout(51);

        stage_SetAnimation(31, 0, 0);
        stage_SetAnimation(25, 0, -1);
        stage_SetAnimation(34, 0, 0);
        stage_SetAnimation(32, 0, 0);
        stage_SetAnimation(35, 0, 0);
        stage_SetAnimation(37, 0, 0);

        capsule_mes[0].func = actE3CapsuleChk;
        sub->mail = capsule_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    } else {
        stage_SetAnimation(31, 0, -1);
        stage_SetAnimation(25, 0, -1);
        stage_SetAnimation(34, 0, 0);
        stage_SetAnimation(32, 0, 0);
        stage_SetAnimation(35, 0, 0);
        stage_SetAnimation(37, 0, 0);
    }
}

static void actE3Capsule_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if ((ICO_WORD)girlGObj == 0) {
        ScpCallCameraOff();
    }

    if (gflagChk(326) == 0) {
        if ((ICO_WORD)boyGObj != 0) {
            scpPlayMot(boyGObj, 0);
        }

        scpFadeOut(255.0f, 0, 0, 0);

        lt_switch_layout(51);

        stage_SetAnimation(31, 0, 0);
        stage_SetAnimation(25, 0, -1);
        stage_SetAnimation(34, 0, 0);
        stage_SetAnimation(32, 0, 0);
        stage_SetAnimation(35, 0, 0);
        stage_SetAnimation(37, 0, 0);

        capsule_mes[0].func = actE3CapsuleChk;
        sub->mail = capsule_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        stage_SetAnimation(31, 0, -1);
        stage_SetAnimation(25, 0, -1);
        stage_SetAnimation(34, 0, 0);
        stage_SetAnimation(32, 0, 0);
        stage_SetAnimation(35, 0, 0);
        stage_SetAnimation(37, 0, 0);
    }
}

void actE3Capsule(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3Capsule_jun26(self);
        return;
    }
    if (ICO_AUG6) {
        actE3Capsule_aug6(self);
        return;
    }
    if (ICO_NTSC) {
        actE3Capsule_ntsc(self);
        return;
    }
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if ((ICO_WORD)girlGObj == 0) {
        ScpCallCameraOff();
    }

    if (gflagChk(PAL_NTSC_AUG6JUN26(358, 326, 325)) == 0) {
        if ((ICO_WORD)boyGObj != 0) {
            scpPlayMot(boyGObj, 0);
        }

        scpFadeOut(255.0f, 0, 0, 0);

        lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));

        stage_SetAnimation(32, 0, 0);
        if (!ICO_NTSCAUG6JUN26) {
            stage_SetAnimation(26, 0, -1);
        }
        stage_SetAnimation(35, 0, 0);
        if (ICO_NTSCAUG6JUN26) {
            stage_SetAnimation(37, 0, 0);
        } else {
            stage_SetAnimation(33, 0, 0);
            stage_SetAnimation(36, 0, 0);
            stage_SetAnimation(38, 0, 0);
        }

        capsule_mes[0].func = actE3CapsuleChk;
        sub->mail = capsule_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        if (ICO_NTSCAUG6JUN26) {
            stage_SetAnimation(31, 0, -1);
            stage_SetAnimation(25, 0, -1);
            stage_SetAnimation(34, 0, 0);
            stage_SetAnimation(32, 0, 0);
        } else {
            stage_SetAnimation(32, 0, -1);
            stage_SetAnimation(26, 0, -1);
        }
        stage_SetAnimation(35, 0, 0);
        if (ICO_NTSCAUG6JUN26) {
            stage_SetAnimation(37, 0, 0);
        } else {
            stage_SetAnimation(33, 0, 0);
            stage_SetAnimation(36, 0, 0);
            stage_SetAnimation(38, 0, 0);
        }
    }
}

/* .sbss: the capsule demo's sub-thread and the flag that thread raises when
   the demo has finished. */
static GProc *capsuleDemoThread; /* derived name */

static int demoEndFlag; /* derived name */

static int demoEnd;

static void actE3CapsuleDemoCancel_jun26(GObj *volatile self)
{
    while (demoEndFlag == 0 || (pad[0].flags & 0x800) == 0) {
        _ACTWait(1);
    }

    iosThreadSetPri(&capsuleDemoThread->thread, 34);

    scpFadeOut(8.0f, 0, 0, 0);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    soundDataClose(e3capsule);

    stage_SetAnimation(27, 0, -1);
    stage_SetAnimation(21, 0, -1);
    stage_SetAnimation(473, -1, -2);
    stage_SetAnimation(478, -1, -2);
    stage_SetAnimation(480, -1, -2);
    stage_SetAnimation(481, -1, -2);
    stage_SetAnimation(482, -1, -2);
    stage_SetAnimation(483, -1, -2);
    stage_SetAnimation(484, -1, -2);
    stage_SetAnimation(485, -1, -2);
    stage_SetAnimation(503, 1, 0);

    scpFadeIn(6.0f);

    actCreateSubThread(actE3CapsuleDemoEnd, 21);
}

void actE3CapsuleDemoCancel(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3CapsuleDemoCancel_jun26(self);
        return;
    }
    while (demoEnd == 0 || (pad[0].flags & 0x800) == 0) {
        _ACTWait(1);
    }

    iosThreadSetPri(&capsuleDemoThread->thread, 34);

    scpFadeOut(8.0f, 0, 0, 0);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    if (ICO_AUG6JUN26) {
        soundDataClose(e3capsule);

        stage_SetAnimation(31, 0, -1);
        stage_SetAnimation(25, 0, -1);
        stage_SetAnimation(522, -1, -2);
        stage_SetAnimation(527, -1, -2);
        stage_SetAnimation(529, -1, -2);
        stage_SetAnimation(530, -1, -2);
        stage_SetAnimation(531, -1, -2);
        stage_SetAnimation(532, -1, -2);
        stage_SetAnimation(533, -1, -2);
        stage_SetAnimation(534, -1, -2);
        stage_SetAnimation(552, 1, 0);
    } else if (ICO_NTSC) {
        soundDataClose(e3capsule);

        stage_SetAnimation(31, 0, -1);
        stage_SetAnimation(25, 0, -1);
        stage_SetAnimation(523, -1, -2);
        stage_SetAnimation(528, -1, -2);
        stage_SetAnimation(530, -1, -2);
        stage_SetAnimation(531, -1, -2);
        stage_SetAnimation(532, -1, -2);
        stage_SetAnimation(533, -1, -2);
        stage_SetAnimation(534, -1, -2);
        stage_SetAnimation(535, -1, -2);
        stage_SetAnimation(553, 1, 0);
    } else if (ICO_JPNKORCN) {
        scpAdpcmCloseFunc(&e3capsule);

        stage_SetAnimation(32, 0, -1);
        stage_SetAnimation(26, 0, -1);
        stage_SetAnimation(590, -1, -2);
        stage_SetAnimation(595, -1, -2);
        stage_SetAnimation(597, -1, -2);
        stage_SetAnimation(598, -1, -2);
        stage_SetAnimation(599, -1, -2);
        stage_SetAnimation(600, -1, -2);
        stage_SetAnimation(601, -1, -2);
        stage_SetAnimation(602, -1, -2);
        stage_SetAnimation(620, 1, 0);
    } else {
        scpAdpcmCloseFunc(&e3capsule);

        stage_SetAnimation(32, 0, -1);
        stage_SetAnimation(26, 0, -1);
        stage_SetAnimation(594, -1, -2);
        stage_SetAnimation(599, -1, -2);
        stage_SetAnimation(601, -1, -2);
        stage_SetAnimation(602, -1, -2);
        stage_SetAnimation(603, -1, -2);
        stage_SetAnimation(604, -1, -2);
        stage_SetAnimation(605, -1, -2);
        stage_SetAnimation(606, -1, -2);
        stage_SetAnimation(624, 1, 0);
    }

    scpFadeIn(6.0f);

    actCreateSubThread(actE3CapsuleDemoEnd, 21);
}

static void actE3CapsuleDemo_ntsc(GObj *volatile self)
{
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);

    demoEnd = 1;
    actCreateSubThread(actE3CapsuleDemoCancel, 21);

    stage_SetAnimation(523, 1, 0);
    _ACTWait(95);
    stage_SetAnimation(31, 1, 0);

    while (stage_ContinueAnimation(523, 528) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 282);

    while (stage_ContinueAnimation(528, 530) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 282);

    while (stage_ContinueAnimation(530, 531) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 286);

    while (stage_ContinueAnimation(531, 532) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 287);

    while (stage_ContinueAnimation(532, 533) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 288);

    while (stage_ContinueAnimation(533, 534) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 289);

    while (stage_ContinueAnimation(534, 535) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 290);

    while (stage_ContinueAnimation(535, 553) == 0) {
        _ACTWait(1);
    }

    demoEnd = 0;
    actCreateSubThread(actE3CapsuleDemoEnd, 21);
}

static void actE3CapsuleDemo_aug6jun26(GObj *volatile self)
{
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);

    if (ICO_JUN26) {
        demoEndFlag = 1;
    } else {
        demoEnd = 1;
    }
    actCreateSubThread(actE3CapsuleDemoCancel, 21);

    stage_SetAnimation(PAL_JUN26(522, 473), 1, 0);
    _ACTWait(95);
    stage_SetAnimation(PAL_JUN26(31, 27), 1, 0);

    while (stage_ContinueAnimation(PAL_JUN26(522, 473), PAL_JUN26(527, 478)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, PAL_JUN26(282, 291));

    while (stage_ContinueAnimation(PAL_JUN26(527, 478), PAL_JUN26(529, 480)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, PAL_JUN26(282, 291));

    while (stage_ContinueAnimation(PAL_JUN26(529, 480), PAL_JUN26(530, 481)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, PAL_JUN26(286, 295));

    while (stage_ContinueAnimation(PAL_JUN26(530, 481), PAL_JUN26(531, 482)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, PAL_JUN26(287, 296));

    while (stage_ContinueAnimation(PAL_JUN26(531, 482), PAL_JUN26(532, 483)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, PAL_JUN26(288, 297));

    while (stage_ContinueAnimation(PAL_JUN26(532, 483), PAL_JUN26(533, 484)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, PAL_JUN26(289, 298));

    while (stage_ContinueAnimation(PAL_JUN26(533, 484), PAL_JUN26(534, 485)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, PAL_JUN26(290, 299));

    while (stage_ContinueAnimation(PAL_JUN26(534, 485), PAL_JUN26(552, 503)) == 0) {
        _ACTWait(1);
    }

    if (ICO_JUN26) {
        demoEndFlag = 0;
    } else {
        demoEnd = 0;
    }
    actCreateSubThread(actE3CapsuleDemoEnd, 21);
}

void actE3CapsuleDemo(GObj *volatile self)
{
    if (ICO_NTSC) {
        actE3CapsuleDemo_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actE3CapsuleDemo_aug6jun26(self);
        return;
    }
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);

    demoEnd = 1;
    actCreateSubThread(actE3CapsuleDemoCancel, 21);

    stage_SetAnimation(PAL_JPNKORCN(594, 590), 1, 0);
    _ACTWait(95);
    stage_SetAnimation(32, 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(594, 590), PAL_JPNKORCN(599, 595)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 288);

    while (stage_ContinueAnimation(PAL_JPNKORCN(599, 595), PAL_JPNKORCN(601, 597)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 288);

    while (stage_ContinueAnimation(PAL_JPNKORCN(601, 597), PAL_JPNKORCN(602, 598)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 292);

    _ACTWait(1);

    stage_SetAnimation(32, 1, 208);

    while (stage_ContinueAnimation(PAL_JPNKORCN(602, 598), PAL_JPNKORCN(603, 599)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 293);

    while (stage_ContinueAnimation(PAL_JPNKORCN(603, 599), PAL_JPNKORCN(604, 600)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 294);

    while (stage_ContinueAnimation(PAL_JPNKORCN(604, 600), PAL_JPNKORCN(605, 601)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 295);

    while (stage_ContinueAnimation(PAL_JPNKORCN(605, 601), PAL_JPNKORCN(606, 602)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 296);

    while (stage_ContinueAnimation(PAL_JPNKORCN(606, 602), PAL_JPNKORCN(624, 620)) == 0) {
        _ACTWait(1);
    }

    demoEnd = 0;
    actCreateSubThread(actE3CapsuleDemoEnd, 21);
}

void actE3St13cInit(void)
{
    SetWayGroupActive(7, 0);

    if (gflagChk(PAL_NTSC_AUG6_JUN26(360, 328, 327, 312)) != 0) {
        SetWayGroupActive(9, 0);
    }
}

static void actE3CageFall_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (gflagChk(312) == 0) {
        stage_SetAnimation(57, 0, 0);
        stage_SetAnimation(53, 0, -1);

        if ((ICO_WORD)girlGObj == 0) {
            _ACTWait(0);
        }

        scpPlayPosSet(girlGObj, -30.0f, -436.0f, -1.0f);

        cageFall_mes[0].func = actE3CageFallChk;
        sub->mail = cageFall_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    } else {
        stage_SetAnimation(57, 0, 0);
        stage_SetAnimation(55, 0, -1);
        stage_SetAnimation(56, 0, -1);
    }
}

void actE3CageFall(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3CageFall_jun26(self);
        return;
    }
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSC_AUG6JUN26(360, 328, 327)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSCAUG6JUN26(76, 72, 63), 0, 0);
        stage_SetAnimation(PAL_JPNKORCN_NTSCAUG6JUN26(72, 68, 59), 0, -1);

        if ((ICO_WORD)girlGObj == 0) {
            _ACTWait(0);
        }

        scpPlayPosSet(girlGObj, -30.0f, -436.0f, -1.0f);

        cageFall_mes[0].func = actE3CageFallChk;
        sub->mail = cageFall_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSCAUG6JUN26(76, 72, 63), 0, 0);
        stage_SetAnimation(PAL_JPNKORCN_NTSCAUG6JUN26(74, 70, 61), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN_NTSCAUG6JUN26(75, 71, 62), 0, -1);
    }
}

static void actE3CageFallChk_jun26(GObj *volatile self)
{
    while (scpTriggerBall(self, boyGObj, 160.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;

    fightSoundProcessRequestPause();
    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    gflagOn(312);

    while (e3cage == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(e3cage->stream);

    actCreateSubThread(actE3CageFallDemo, 21);

    stage_SetAnimation(55, 1, 0);
    stage_SetAnimation(56, 1, 0);

    actCreateSubThread(actE3CageFallEffect, 21);

    while (stage_CheckAnimationFinish(55) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    SetWayGroupActive(9, 0);
}

void actE3CageFallChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3CageFallChk_jun26(self);
        return;
    }
    while (scpTriggerBall(self, boyGObj, 160.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));

    scpBoyControlReadDisable = 1;

    if (!ICO_NTSCAUG6JUN26) {
        ((Act *)scpSearchGobj(PAL_JPNKORCN(3229, 3206))->act)->flags20.ll |= 0x20000;
        ((Act *)scpSearchGobj(PAL_JPNKORCN(3230, 3207))->act)->flags20.ll |= 0x20000;
    }

    fightSoundProcessRequestPause();
    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    gflagOn(PAL_NTSC_AUG6JUN26(360, 328, 327));

    while (e3cage == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(e3cage->stream);

    actCreateSubThread(actE3CageFallDemo, 21);

    if (ICO_NTSCAUG6JUN26) {
        stage_SetAnimation(61, 1, 0);
        stage_SetAnimation(62, 1, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(74, 70), 1, 0);
        stage_SetAnimation(PAL_JPNKORCN(75, 71), 1, 0);
    }

    actCreateSubThread(actE3CageFallEffect, 21);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSCAUG6JUN26(74, 70, 61)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    SetWayGroupActive(9, 0);
}

static void actE3CageFallDemo_ntsc(GObj *volatile self)
{
    float dir[4];

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    stage_SetAnimation(559, 1, 0);

    scpPlayMot(boyGObj, 309);

    while (stage_ContinueAnimation(559, 560) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(560, 561) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(girlGObj, 681);

    while (stage_ContinueAnimation(561, 562) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 682);

    while (stage_ContinueAnimation(562, 563) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 310);

    while (stage_ContinueAnimation(563, 564) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 319);

    while (stage_ContinueAnimation(564, 565) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 683);

    while (stage_ContinueAnimation(565, 566) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 684);

    _ACTWait(240);

    gflagOn(332);

    fightSoundProcessRequestStart();

    while (stage_ContinueAnimation(566, 802) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 317);

    while (stage_ContinueAnimation(802, 803) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 320);

    while (stage_CheckAnimationFrame(803, 150, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 318);

    while (stage_CheckAnimationFinish(803) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpPlayEnd(boyGObj);

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);

    gflagOn(348);

    gamesysObjInfoPosSetStage(boyGObj, GOBJ_ACT(boyGObj)->infoPos, 0, stage_no);

    CheckPoint();

    gflagOff(348);
}

static void actE3CageFallDemo_jun26(GObj *volatile self)
{
    float dir[4];

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    stage_SetAnimation(509, 1, 0);

    scpPlayMot(boyGObj, 318);

    while (stage_ContinueAnimation(509, 510) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(510, 511) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(girlGObj, 707);

    while (stage_ContinueAnimation(511, 512) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 708);

    while (stage_ContinueAnimation(512, 513) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 319);

    while (stage_ContinueAnimation(513, 514) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 328);

    while (stage_ContinueAnimation(514, 515) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 709);

    while (stage_ContinueAnimation(515, 516) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 710);

    _ACTWait(240);

    gflagOn(316);

    fightSoundProcessRequestStart();

    while (stage_ContinueAnimation(516, 750) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 326);

    while (stage_ContinueAnimation(750, 751) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 329);

    while (stage_CheckAnimationFrame(751, 150, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 327);

    while (stage_CheckAnimationFinish(751) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpPlayEnd(boyGObj);

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);

    gflagOn(332);

    gamesysObjInfoPosSetStage(boyGObj, GOBJ_ACT(boyGObj)->infoPos, 0, stage_no);

    CheckPoint();

    gflagOff(332);
}

static void actE3CageFallDemo_aug6(GObj *volatile self)
{
    float dir[4];

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    stage_SetAnimation(558, 1, 0);

    scpPlayMot(boyGObj, 309);

    while (stage_ContinueAnimation(558, 559) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(559, 560) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(girlGObj, 683);

    while (stage_ContinueAnimation(560, 561) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 684);

    while (stage_ContinueAnimation(561, 562) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 310);

    while (stage_ContinueAnimation(562, 563) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 319);

    while (stage_ContinueAnimation(563, 564) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 685);

    while (stage_ContinueAnimation(564, 565) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 686);

    _ACTWait(240);

    gflagOn(331);

    fightSoundProcessRequestStart();

    while (stage_ContinueAnimation(565, 801) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 317);

    while (stage_ContinueAnimation(801, 802) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 320);

    while (stage_CheckAnimationFrame(802, 150, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 318);

    while (stage_CheckAnimationFinish(802) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpPlayEnd(boyGObj);

    scpBoyControlReadDisable = 0;

    lt_switch_layout(50);

    gflagOn(347);

    gamesysObjInfoPosSetStage(boyGObj, GOBJ_ACT(boyGObj)->infoPos, 0, stage_no);

    CheckPoint();

    gflagOff(347);
}

void actE3CageFallDemo(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3CageFallDemo_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actE3CageFallDemo_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actE3CageFallDemo_aug6(self);
        return;
    }
    float dir[4];

    lt_switch_layout(PAL_JPNKORCN(55, 52));

    scpBoyControlReadDisable = 1;

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    stage_SetAnimation(PAL_JPNKORCN(630, 626), 1, 0);

    scpPlayMot(boyGObj, 315);

    while (stage_ContinueAnimation(PAL_JPNKORCN(630, 626), PAL_JPNKORCN(631, 627)) == 0) {
        _ACTWait(1);
    }

    SetHandCameraLimitInDemo(0, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(631, 627), PAL_JPNKORCN(632, 628)) == 0) {
        _ACTWait(1);
    }

    ResetHandCameraLimitInDemo();

    scpPlayMot(girlGObj, 726);

    while (stage_ContinueAnimation(PAL_JPNKORCN(632, 628), PAL_JPNKORCN(633, 629)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 727);

    while (stage_ContinueAnimation(PAL_JPNKORCN(633, 629), PAL_JPNKORCN(634, 630)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 316);

    while (stage_ContinueAnimation(PAL_JPNKORCN(634, 630), PAL_JPNKORCN(635, 631)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 325);

    while (stage_ContinueAnimation(PAL_JPNKORCN(635, 631), PAL_JPNKORCN(636, 632)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 728);

    while (stage_ContinueAnimation(PAL_JPNKORCN(636, 632), PAL_JPNKORCN(637, 633)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 729);

    _ACTWait(240);

    gflagOn(364);

    fightSoundProcessRequestStart();

    while (stage_ContinueAnimation(PAL_JPNKORCN(637, 633), PAL_JPNKORCN(955, 936)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 323);

    while (stage_ContinueAnimation(PAL_JPNKORCN(955, 936), PAL_JPNKORCN(956, 937)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(girlGObj, 326);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(956, 937), 150, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 324);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(956, 937)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpPlayEnd(boyGObj);

    scpBoyControlReadDisable = 0;

    lt_switch_layout(PAL_JPNKORCN(54, 51));

    scpPlayMot(girlGObj, 532);
    {
        GObj *obj = girlGObj;

        GOBJ_SUB(obj)->ctrl.blendFrames =
            (int)((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f * 30.0f);

        scpPlayEnd(obj);
    }

    gflagOn(381);

    gamesysObjInfoPosSetStage(boyGObj, GOBJ_ACT(boyGObj)->infoPos, 0, stage_no);

    CheckPoint();

    gflagOff(381);
}

inline void actE3CapsuleDemoEnd(GObj *volatile self)
{
    scpPlayMot(boyGObj, PAL_NTSCAUG6_JUN26(307, 301, 310));

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6_JUN26(624, 620, 553, 552, 503)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);

    {
        EffectArg ofs = {{-1000.0f, 0.0f, -2200.0f, 1.0f}};
        float dir[4];

        sceVu0SubVector(dir, &ofs, test_CURRENTROOT(boyGObj));
        scpPlayMotDir(boyGObj, dir);
    }

    scpPlayEnd(boyGObj);

    scpBoyControlReadDisable = 0;

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));

    enable_game_pause = 1;
}

void actE3CageFallEffect(GObj *volatile self)
{
    float t;
    float tn;
    int n;

    t = 0.0f;
    do {
        switch ((int)t) {
        case 0:
            gflagOn(PAL_NTSCAUG6JUN26(22, 21));
            break;
        case 64:
            iosPadActRequest(boyPad, 17);
            {
                EffectArg b1 = {{-88.0f, -50.0f, -1.0f, 1.0f}};
                scpEffectStart(&b1, 0);
            }
            {
                EffectArg b2 = {{-96.0f, -45.0f, 34.0f, 1.0f}};
                scpEffectStart(&b2, 0);
            }
            break;
        case 68: {
            EffectArg b3 = {{72.0f, -50.0f, 8.0f, 1.0f}};
            scpEffectStart(&b3, 0);
        }
            {
                EffectArg b4 = {{80.0f, -50.0f, 2.0f, 1.0f}};
                scpEffectStart(&b4, 0);
            }
            break;
        case 96: {
            EffectArg b5 = {{-27.0f, -50.0f, 100.0f, 1.0f}};
            scpEffectStart(&b5, 0);
        }
            {
                EffectArg b6 = {{-56.0f, -50.0f, 66.0f, 1.0f}};
                scpEffectStart(&b6, 0);
            }
            {
                EffectArg b7 = {{-5.0f, -50.0f, 42.0f, 1.0f}};
                scpEffectStart(&b7, 0);
            }
            break;
        case 180:
            iosPadActRequest(boyPad, 15);
            break;
        case 300: {
            EffectArg b8 = {{-10.0f, 0.0f, 466.0f, 1.0f}};
            scpEffectStart(&b8, 0);
        }
            {
                EffectArg b9 = {{-25.0f, 0.0f, 450.0f, 1.0f}};
                scpEffectStart(&b9, 0);
            }
            break;
        case 380:
            iosPadActRequest(boyPad, 16);
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
    } while (t < 400.0f);
}

static void actE3St09aSekizo_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    soundSeDefPlay(1319, 0, st09aSekizoPos, 1);
    soundSeDefPlay(1320, 0, st09aSekizoPos, 1);
    soundSeDefPlay(1321, 0, st09aSekizoPos, 1);

    if (gflagChk(333) == 0) {
        if (girlGObj == 0) {
            ScpCallCameraOff();
        }
        stage_SetAnimation(341, 0, 0);

        st09aSekizo_mes[0].func = actE3St09aSekizoChk;
        sub->mail = st09aSekizo_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        ScpCallCameraSetTarget(1421.0f, 97.0f, -1885.0f);
        stage_SetAnimation(341, 0, -1);
    }
}

static void actE3St09aSekizo_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    soundSeDefPlay(1290, 0, st09aSekizoPos, 1);
    soundSeDefPlay(1291, 0, st09aSekizoPos, 1);
    soundSeDefPlay(1292, 0, st09aSekizoPos, 1);

    if (gflagChk(317) == 0) {
        stage_SetAnimation(307, 0, 0);

        st09aSekizo_mes[0].func = actE3St09aSekizoChk;
        sub->mail = st09aSekizo_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    } else {
        stage_SetAnimation(307, 0, -1);
    }
}

static void actE3St09aSekizo_aug6(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    soundSeDefPlay(1319, 0, st09aSekizoPos, 1);
    soundSeDefPlay(1320, 0, st09aSekizoPos, 1);
    soundSeDefPlay(1321, 0, st09aSekizoPos, 1);

    if (gflagChk(332) == 0) {
        if (girlGObj == 0) {
            ScpCallCameraOff();
        }
        stage_SetAnimation(340, 0, 0);

        st09aSekizo_mes[0].func = actE3St09aSekizoChk;
        sub->mail = st09aSekizo_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    } else {
        ScpCallCameraSetTarget(1421.0f, 97.0f, -1885.0f);
        stage_SetAnimation(340, 0, -1);
    }
}

void actE3St09aSekizo(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3St09aSekizo_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actE3St09aSekizo_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actE3St09aSekizo_aug6(self);
        return;
    }
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    soundSeDefPlay(1346, 0, st09aSekizoPos, 1);
    soundSeDefPlay(1347, 0, st09aSekizoPos, 1);
    soundSeDefPlay(1348, 0, st09aSekizoPos, 1);

    if (gflagChk(366) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(376, 372), 0, 0);

        st09aSekizo_mes[0].func = actE3St09aSekizoChk;
        sub->mail = st09aSekizo_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        ScpCallCameraSetTarget(1421.0f, 97.0f, -1885.0f);
        stage_SetAnimation(PAL_JPNKORCN(376, 372), 0, -1);
    }
}

inline void actE3St09aGirlWay(GObj *volatile self)
{
    EffectArg buf = {{-1410.0f, -100.0f, 1950.0f, 0.0f}};
    long long way[2];

    _SCPMoveCharactorByWay(girlGObj, 0, buf.f, 100.0f, 0);

    memset(way, 0, 16);
    RequestStageChangeDirect(girlGObj, PAL_JPNKORNTSCAUG6CN_JUN26(102, 100, 99), way, 180);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));

    scpBoyControlReadDisable = 0;
    brainUnlockGirl();
}

static void actE3St09aSekizoChk_jun26(GObj *volatile self)
{
    float dir[4];

    if ((ICO_WORD)girlGObj == 0) {
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

    gflagOn(317);

    _ACTWait(60);

    stage_SetAnimation(307, 1, 0);

    sekizo_e3_ntscaug6jun26 = iosPadActRequest(boyPad, 9);
    sekizo_e3_vol_ntscaug6jun26 = 128;
    iosPadActVolumeSet(sekizo_e3_ntscaug6jun26, 128);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayPosSet(girlGObj, -1410.0f, -100.0f, 1515.0f);
    scpPlayPosSet(boyGObj, -1330.0f, -100.0f, 1515.0f);

    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpBoyControlReadDisable = 1;

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpPlayMot(girlGObj, 584);
    scpPlayWaitMotEnd(girlGObj);

    while (stage_CheckAnimationFrame(307, 151, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(sekizo_e3_ntscaug6jun26);

    scpPlayMot(girlGObj, 521);
    scpPlayEnd(girlGObj);

    actCreateSubThread(actE3St09aGirlWay, 21);

    _ACTWait(30);

    scpPlayMot(boyGObj, 254);
    scpPlayWaitMotEnd(boyGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
}

void actE3St09aSekizoChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3St09aSekizoChk_jun26(self);
        return;
    }
    float dir[4];

    if ((ICO_WORD)girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0 ||
           ForMotionViewer_GetCurrentMotion(boyGObj) == 75) {
        _ACTWait(1);
    }

    scpSekizouCheckPoint();

    scpAdpcmPlayRequestFunc(18, 0, 1, 1, 1);

    scpKillEnemyAll();
    scpMaskGeneratorAll();

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));

    scpBoyControlReadDisable = 1;
    brainLockGirl();

    gflagOn(PAL_NTSC_AUG6JUN26(366, 333, 332));

    _ACTWait(60);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(376, 372, 341, 340), 1, 0);

    if (ICO_NTSCAUG6JUN26) {
        sekizo_e3_ntscaug6jun26 = iosPadActRequest(boyPad, 9);
        sekizo_e3_vol_ntscaug6jun26 = 128;
        iosPadActVolumeSet(sekizo_e3_ntscaug6jun26, 128);
    } else {
        sekizo_e3_vol = iosPadActRequest(boyPad, 9);
        e3sekizo_yure_vol = 128;
        iosPadActVolumeSet(sekizo_e3_vol, 128);
    }

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    scpPlayPosSet(girlGObj, -1410.0f, -100.0f, 1515.0f);
    scpPlayPosSet(boyGObj, -1330.0f, -100.0f, 1515.0f);

    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpBoyControlReadDisable = 1;

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpPlayMot(girlGObj, PAL_NTSC_AUG6JUN26(645, 572, 573));
    scpPlayWaitMotEnd(girlGObj);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6JUN26(376, 372, 341, 340), 151, 0) ==
           0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(PAL_NTSCAUG6JUN26(sekizo_e3_vol, sekizo_e3_ntscaug6jun26));

    scpPlayMot(girlGObj, PAL_NTSCAUG6JUN26(532, 514));
    scpPlayEnd(girlGObj);

    actCreateSubThread(actE3St09aGirlWay, 21);

    _ACTWait(30);

    scpPlayMot(boyGObj, PAL_NTSCAUG6JUN26(252, 247));
    scpPlayWaitMotEnd(boyGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    ScpCallCameraSetTarget(1421.0f, 97.0f, -1885.0f);
}

/* stream-motion-def's table; streamMotionManager.h, which defines the
   record, does not declare it */
extern StreamMotionFile streamMotion[];

static void actE3GateChk_jun26(GObj *volatile self)
{
    int i;

    if ((ICO_WORD)girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 3000.0f) == 0) {
        _ACTWait(1);
    }

    StandbyStreamMotion(streamMotion[9].path);

    i = 0;
    while (CheckReadyStreamMotion() == 0) {
        i++;
        debug_StdPrintfDummy("Now waiting for standby stream motion system... %d\n", i);
        _ACTWait(1);
    }

    lt_switch_layout(51);

    SetWindManager(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 5.0f, 0.0f);

    scpPlayStart(girlGObj);

    reg_SetScissorSw(1);
    SetStaticBlur(0);

    scpAdpcmPlayRequestFunc(7, &e3gate1st, 1, 1, 0);
    while (e3gate1st == 0) {
        _ACTWait(1);
    }

    gflagOn(313);
    _ACTWait(1);

    actCreateSubThread(actE3GateDemo, 21);
    actCreateSubThread(actE3GateJimaku, 21);

    stage_SetAnimation(212, 1, 0);
}

void actE3GateChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3GateChk_jun26(self);
        return;
    }
    int i;

    if ((ICO_WORD)girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 3000.0f) == 0) {
        _ACTWait(1);
    }

    StandbyStreamMotion(streamMotion[9].path);

    i = 0;
    while (CheckReadyStreamMotion() == 0) {
        i++;
        debug_StdPrintfDummy("Now waiting for standby stream motion system... %d\n", i);
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));

    SetWindManager(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 5.0f, 0.0f);

    scpPlayStart(girlGObj);

    reg_SetScissorSw(1);
    SetStaticBlur(0);

    scpAdpcmPlayRequestFunc(4, &e3gate1st, 1, 1, 0);
    while (e3gate1st == 0) {
        _ACTWait(1);
    }

    gflagOn(PAL_NTSC_AUG6JUN26(361, 329, 328));
    _ACTWait(1);

    actCreateSubThread(actE3GateDemo, 21);
    actCreateSubThread(actE3GateJimaku, 21);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(269, 265, 238, 237), 1, 0);
}

static void actE3GateDemo_ntsc(GObj *volatile self)
{
    scpSearchGobj(3127)->active = 1;

    EntryStreamMotion(boyGObj);
    EntryStreamMotion(girlGObj);
    EntryStreamMotion(scpSearchGobj(3127));

    PlayStreamMotion();

    scpFadeIn(6.0f);

    stage_SetAnimation(577, 1, 0);

    while (stage_CheckAnimationFrame(577, 10, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    AdpcmPlay(e3gate1st->stream);

    while (stage_ContinueAnimation(577, 578) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(578, 579) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(579, 580) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(580, 581) == 0) {
        _ACTWait(1);
    }
    if (!ICO_JPNKORCN) {
        while (stage_ContinueAnimation(581, 582) == 0) {
            _ACTWait(1);
        }

        reg_SetScissorSw(0);

        while (stage_ContinueAnimation(582, 583) == 0) {
            _ACTWait(1);
        }
        while (stage_ContinueAnimation(583, 584) == 0) {
            _ACTWait(1);
        }
        while (stage_ContinueAnimation(584, 585) == 0) {
            _ACTWait(1);
        }
    }

    SetStaticBlur(8);
    SetAuraInspireParam(100.0f);

    stage_SetAnimation(411, 1, 0);

    iosPadActRequest(boyPad, 15);

    soundSeDefPlay(1388, 0, 0, 0);

    while (stage_CheckAnimationFrame(585, 45, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (e3gate1st != 0) {
        scpAdpcmFadeClose(e3gate1st, 80);
    }

    ReinitWindManager();

    lt_switch_layout(50);

    enable_game_pause = 0;

    scpFadeOut(4.0f, 255, 255, 255);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    while (scpSeEnvMasterVolRate > 0.0f) {
        scpSeEnvMasterVolRate -= 0.012f;
        _ACTWait(1);
    }

    mpegPlayReturnStage = 93;

    stgmgrForceSwitchWithFadeColor(102, 255.0f, 4.0f, 255, 255, 255);
}

static void actE3GateDemo_jun26(GObj *volatile self)
{
    scpSearchGobj(2942)->active = 1;

    EntryStreamMotion(boyGObj);
    EntryStreamMotion(girlGObj);
    EntryStreamMotion(scpSearchGobj(2942));

    PlayStreamMotion();

    scpFadeIn(6.0f);

    stage_SetAnimation(527, 1, 0);

    while (stage_CheckAnimationFrame(527, 10, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    AdpcmPlay(e3gate1st->stream);

    while (stage_ContinueAnimation(527, 528) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(528, 529) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(529, 530) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(530, 531) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(531, 532) == 0) {
        _ACTWait(1);
    }

    reg_SetScissorSw(0);

    while (stage_ContinueAnimation(532, 533) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(533, 534) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(534, 535) == 0) {
        _ACTWait(1);
    }

    SetStaticBlur(8);
    SetAuraInspireParam(100.0f);

    stage_SetAnimation(375, 1, 0);

    iosPadActRequest(boyPad, 15);

    soundSeDefPlay(1336, 0, 0, 0);

    while (stage_CheckAnimationFrame(535, 45, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (e3gate1st != 0) {
        scpAdpcmFadeClose(e3gate1st, 80);
    }

    ReinitWindManager();

    lt_switch_layout(50);

    scpFadeOut(4.0f, 255, 255, 255);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    while (scpSeEnvMasterVolRate > 0.0f) {
        scpSeEnvMasterVolRate -= 0.012f;
        _ACTWait(1);
    }

    InterStageSwitchLock++;
    mpegPlayReturnStage = 92;

    stgmgrForceSwitchWithFadeColor(101, 255.0f, 4.0f, 255, 255, 255);
}

static void actE3GateDemo_aug6(GObj *volatile self)
{
    scpSearchGobj(3097)->active = 1;

    EntryStreamMotion(boyGObj);
    EntryStreamMotion(girlGObj);
    EntryStreamMotion(scpSearchGobj(3097));

    PlayStreamMotion();

    scpFadeIn(6.0f);

    stage_SetAnimation(576, 1, 0);

    while (stage_CheckAnimationFrame(576, 10, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    AdpcmPlay(e3gate1st->stream);

    while (stage_ContinueAnimation(576, 577) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(577, 578) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(578, 579) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(579, 580) == 0) {
        _ACTWait(1);
    }
    if (!ICO_JPNKORCN) {
        while (stage_ContinueAnimation(580, 581) == 0) {
            _ACTWait(1);
        }

        reg_SetScissorSw(0);

        while (stage_ContinueAnimation(581, 582) == 0) {
            _ACTWait(1);
        }
        while (stage_ContinueAnimation(582, 583) == 0) {
            _ACTWait(1);
        }
        while (stage_ContinueAnimation(583, 584) == 0) {
            _ACTWait(1);
        }
    }

    SetStaticBlur(8);
    SetAuraInspireParam(100.0f);

    stage_SetAnimation(410, 1, 0);

    iosPadActRequest(boyPad, 15);

    soundSeDefPlay(1388, 0, 0, 0);

    while (stage_CheckAnimationFrame(584, 45, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (e3gate1st != 0) {
        scpAdpcmFadeClose(e3gate1st, 80);
    }

    ReinitWindManager();

    lt_switch_layout(50);

    enable_game_pause = 0;

    scpFadeOut(4.0f, 255, 255, 255);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    while (scpSeEnvMasterVolRate > 0.0f) {
        scpSeEnvMasterVolRate -= 0.012f;
        _ACTWait(1);
    }

    mpegPlayReturnStage = 93;

    stgmgrForceSwitchWithFadeColor(102, 255.0f, 4.0f, 255, 255, 255);
}

void actE3GateDemo(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3GateDemo_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actE3GateDemo_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actE3GateDemo_aug6(self);
        return;
    }
    scpSearchGobj(PAL_JPNKORCN(3382, 3359))->active = 1;

    EntryStreamMotion(boyGObj);
    EntryStreamMotion(girlGObj);
    EntryStreamMotion(scpSearchGobj(PAL_JPNKORCN(3382, 3359)));

    PlayStreamMotion();

    scpFadeIn(6.0f);

    stage_SetAnimation(PAL_JPNKORCN(648, 644), 1, 0);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(648, 644), 10, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    AdpcmPlay(e3gate1st->stream);

    if (ICO_JPNKORCN) {
        while (stage_ContinueAnimation(644, 645) == 0) {
            _ACTWait(1);
        }
        while (stage_ContinueAnimation(645, 646) == 0) {
            _ACTWait(1);
        }
        while (stage_ContinueAnimation(646, 647) == 0) {
            _ACTWait(1);
        }
        while (stage_ContinueAnimation(647, 648) == 0) {
            _ACTWait(1);
        }
    }
    while (stage_ContinueAnimation(648, 649) == 0) {
        _ACTWait(1);
    }

    if (ICO_JPNKORCN) {
        reg_SetScissorSw(0);
    }

    while (stage_ContinueAnimation(649, 650) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(650, 651) == 0) {
        _ACTWait(1);
    }
    while (stage_ContinueAnimation(651, 652) == 0) {
        _ACTWait(1);
    }
    if (!ICO_JPNKORCN) {
        while (stage_ContinueAnimation(652, 653) == 0) {
            _ACTWait(1);
        }

        reg_SetScissorSw(0);

        while (stage_ContinueAnimation(653, 654) == 0) {
            _ACTWait(1);
        }
        while (stage_ContinueAnimation(654, 655) == 0) {
            _ACTWait(1);
        }
        while (stage_ContinueAnimation(655, 656) == 0) {
            _ACTWait(1);
        }
    }

    SetStaticBlur(8);
    SetAuraInspireParam(100.0f);

    stage_SetAnimation(PAL_JPNKORCN(475, 471), 1, 0);

    iosPadActRequest(boyPad, 15);

    soundSeDefPlay(1425, 0, 0, 0);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(656, 652), 45, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    if (e3gate1st != 0) {
        scpAdpcmFadeCloseFunc(&e3gate1st, 80);
    }

    ReinitWindManager();

    lt_switch_layout(PAL_JPNKORCN(54, 51));

    enable_game_pause = 0;

    scpFadeOut(4.0f, 255, 255, 255);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    while (scpSeEnvMasterVolRate > 0.0f) {
        scpSeEnvMasterVolRate -= 0.012f;
        _ACTWait(1);
    }

    mpegPlayReturnStage = PAL_JPNKORCN(95, 93);

    stgmgrForceSwitchWithFadeColor(PAL_JPNKORCN(104, 102), 255.0f, 4.0f, PAL_JPNKORCN(0, 255),
                                   PAL_JPNKORCN(0, 255), PAL_JPNKORCN(0, 255));
}

void actE3GateJimaku(GObj *volatile self)
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
        case 300:
            jimaku_msg.sub.block = 25;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 1420:
            jimaku_msg.sub.block = 26;
            jimaku_msg.sub.jump = 30;
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
    } while (t < 7300.0f);
}

void actE3St01bInit(void)
{
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(183, 179, 162, 161, 147), 0, -1);
}

static void actE3St09aBrgDown(GObj *volatile self)
{
    lt_switch_layout(PAL_JPNKORCN(55, 52));
    gflagOn(86);

    scpAdpcmPlayRequestFunc(90, &sekizo_e3, 1, 1, 1);
    while (sekizo_e3 == 0) {
        _ACTWait(1);
    }

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

    scpBoyControlReadDisable = 0;

    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

static void actE3Warning_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self); /* only the Japanese and Korean arm reads it */

    scpFadeOut(255.0f, 0, 0, 0);

    enable_game_pause = 0;

    _ACTWait(1);

    scpBoyControlReadDisable = 1;
    InitStageLight(stage_no);
    if (ICO_JPN) {
        GlobalStageSetting.viewScale = 150;
    }

    scpFadeIn(6.0f);

    if (boyGObj != 0) {
        scpPlayMot(boyGObj, 0);
    }

    warning_mes[0].func = actE3WarningChk;
    sub->mail = warning_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static void actE3Warning_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self); /* only the Japanese and Korean arm reads it */

    _ACTWait(1);

    enable_game_pause = 0;
    scpBoyControlReadDisable = 1;

    if (boyGObj != 0) {
        scpPlayMot(boyGObj, 0);
    }

    warning_mes[0].func = actE3WarningChk;
    sub->mail = warning_mes;
    ACTSendMailCorrect(self, 377);
    _ACTWait(0);
}

static void actE3Warning_aug6(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self); /* only the Japanese and Korean arm reads it */

    scpFadeOut(255.0f, 0, 0, 0);

    enable_game_pause = 0;

    _ACTWait(1);

    scpBoyControlReadDisable = 1;
    InitStageLight(stage_no);
    if (ICO_JPN) {
        GlobalStageSetting.viewScale = 150;
    }

    scpFadeIn(6.0f);

    if (boyGObj != 0) {
        scpPlayMot(boyGObj, 0);
    }

    warning_mes[0].func = actE3WarningChk;
    sub->mail = warning_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actE3Warning(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3Warning_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actE3Warning_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actE3Warning_aug6(self);
        return;
    }
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self); /* only the Japanese and Korean arm reads it */

    scpFadeOut(255.0f, 0, 0, 0);

    enable_game_pause = 0;

    _ACTWait(1);

    scpBoyControlReadDisable = 1;
    InitStageLight(stage_no);
    if (ICO_JPN) {
        GlobalStageSetting.viewScale = 150;
    }

    if (ICO_JPNKORCN) {
        scpFadeIn(6.0f);

        if ((ICO_WORD)boyGObj != 0) {
            scpPlayMot(boyGObj, 0);
        }

        warning_mes[0].func = actE3WarningChk;
        sub->mail = warning_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        RequestStageChange(1, boyGObj, 0, 255.0f, 0.0f);
    }
}

static void actE3Inst1_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);
    const StgPre *pre;

    _ACTWait(1);

    scpBoyControlReadDisable = 1;
    if ((ICO_WORD)boyGObj != 0) {
        scpPlayMot(boyGObj, 0);
    }

    pre = &stageData[stage_no];
    stgmgrNextStagePreLoadForceStageSet(exitData[pre->ent[0]].nextStage);

    inst1_mes[0].func = actE3Inst1Chk;
    sub->mail = inst1_mes;
    ACTSendMailCorrect(self, 377);
    _ACTWait(0);
}

void actE3Inst1(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3Inst1_jun26(self);
        return;
    }
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);
    const StgPre *pre;

    _ACTWait(1);

    scpBoyControlReadDisable = 1;
    if ((ICO_WORD)boyGObj != 0) {
        scpPlayMot(boyGObj, 0);
    }

    pre = &stageData[stage_no];
    stgmgrNextStagePreLoadForceStageSet(exitData[pre->ent[0]].nextStage);

    inst1_mes[0].func = actE3Inst1Chk;
    sub->mail = inst1_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

void actE3Door(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSC_AUG6_JUN26(359, 327, 326, 311)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(953, 934, 800, 799, 748), 0, 0);

        door_mes[0].func = actE3DoorMain;
        sub->mail = door_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(953, 934, 800, 799, 748), 0, -1);
    }
}

void actE3CageFallReady(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSC_AUG6_JUN26(360, 328, 327, 312)) == 0) {
        cageFallReady_mes[0].func = actE3CageFallReadyChk;
        sub->mail = cageFallReady_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
        _ACTWait(0);
    }
}

static void actE3St13cIntro_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (girlGObj == 0) {
        ScpCallCameraOff();
    }

    if (gflagChk(331) == 0) {
        st13cIntro_mes[0].func = actE3St13cIntroChk;
        sub->mail = st13cIntro_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actE3St13cIntro_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (gflagChk(315) == 0) {
        st13cIntro_mes[0].func = actE3St13cIntroChk;
        sub->mail = st13cIntro_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    }
}

static void actE3St13cIntro_aug6(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (girlGObj == 0) {
        ScpCallCameraOff();
    }

    if (gflagChk(330) == 0) {
        st13cIntro_mes[0].func = actE3St13cIntroChk;
        sub->mail = st13cIntro_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actE3St13cIntro(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3St13cIntro_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actE3St13cIntro_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actE3St13cIntro_aug6(self);
        return;
    }
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (gflagChk(363) == 0) {
        st13cIntro_mes[0].func = actE3St13cIntroChk;
        sub->mail = st13cIntro_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actE3St13cGene1(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(PAL_NTSC_AUG6_JUN26(364, 332, 331, 316)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(400);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_MaskOff(self);
}

void actE3St13cGene2(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(PAL_NTSC_AUG6_JUN26(364, 332, 331, 316)) == 0) {
        _ACTWait(1);
    }
    Generator_Call(self);
    _ACTWait(120);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_MaskOff(self);
}

void actE3Floor(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
}

static void actE3St01bEne_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (girlGObj == 0) {
        ScpCallCameraOff();
    }

    if (gflagChk(330) == 0) {
        st01bEne_mes[0].func = actE3St01bEneChk;
        sub->mail = st01bEne_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actE3St01bEne_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (gflagChk(314) == 0) {
        st01bEne_mes[0].func = actE3St01bEneChk;
        sub->mail = st01bEne_mes;
        ACTSendMailCorrect(self, 377);
        _ACTWait(0);
    }
}

static void actE3St01bEne_aug6(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (girlGObj == 0) {
        ScpCallCameraOff();
    }

    if (gflagChk(329) == 0) {
        st01bEne_mes[0].func = actE3St01bEneChk;
        sub->mail = st01bEne_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actE3St01bEne(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3St01bEne_jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actE3St01bEne_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actE3St01bEne_aug6(self);
        return;
    }
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (gflagChk(362) == 0) {
        st01bEne_mes[0].func = actE3St01bEneChk;
        sub->mail = st01bEne_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actE3St01bGene1(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(PAL_NTSC_AUG6_JUN26(362, 330, 329, 314)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(30);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_MaskOff(self);
}

void actE3St01bGene2(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(PAL_NTSC_AUG6_JUN26(362, 330, 329, 314)) == 0) {
        _ACTWait(1);
    }
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_MaskOff(self);
}

void actE3St01bGene3(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    while (gflagChk(PAL_NTSC_AUG6_JUN26(362, 330, 329, 314)) == 0) {
        _ACTWait(1);
    }
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_MaskOff(self);
}

void actE3St09aBrg(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (gflagChk(86) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(378, 374), 0, 0);

        st09aBrg_mes[0].func = actE3St09aBrgMain;
        sub->mail = st09aBrg_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(378, 374), 0, -1);
    }
}

static void actE3Gate_jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    scpSearchGobj(2942)->active = 0;

    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    scpPlayMot(boyGObj, 0);

    scpFadeOut(255.0f, 0, 0, 0);

    stage_SetAnimation(212, 0, 0);
    stage_SetAnimation(215, 0, 0);

    gate_mes[0].func = actE3GateChk;
    sub->mail = gate_mes;
    ACTSendMailCorrect(self, 377);
    _ACTWait(0);
}

void actE3Gate(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3Gate_jun26(self);
        return;
    }
    GObj *x = self;
    Act *sub = (Act *)actInitialize(self);

    _ACTWait(1);

    if (ICO_AUG6JUN26) {
        scpSearchGobj(3097)->active = 0;
    } else if (ICO_JPNKORCN) {
        MallocStreamMotionBuffer();

        scpSearchGobj(3359)->active = 0;
    } else if (ICO_NTSC) {
        MallocStreamMotionBuffer();

        scpSearchGobj(3127)->active = 0;
    } else {
        MallocStreamMotionBuffer();

        scpSearchGobj(3382)->active = 0;
    }

    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    scpPlayMot(boyGObj, 0);

    scpFadeOut(255.0f, 0, 0, 0);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(269, 265, 238, 237), 0, 0);
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(272, 268, 241, 240), 0, 0);

    gate_mes[0].func = actE3GateChk;
    sub->mail = gate_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

void actE3CapsuleChk(GObj *volatile self)
{
    scpSekizouCheckPoint();

    gflagOn(PAL_NTSC_AUG6_JUN26(358, 326, 325, 310));

    scpAdpcmPlayRequestFunc(PAL_JUN26(2, 5), &e3capsule, 1, 1, 1);
    while (e3capsule == 0) {
        _ACTWait(1);
    }

    scpFadeIn(6.0f);

    capsuleDemoThread = actCreateSubThread(actE3CapsuleDemo, 21);
}

inline void actE3DoorMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 0;

    sub->mainMail = doorMain_mes;

    while (1) {
        _ACTWait(1);
    }
}

inline void actE3DoorSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    doorSwitch_mes[0].func = actE3DoorUp;
    sub->mail = doorSwitch_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6_JUN26(430, 397, 393, 377));
    _ACTWait(0);
}

inline void actE3DoorUp(GObj *volatile self)
{
    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
    gflagOn(PAL_NTSC_AUG6_JUN26(359, 327, 326, 311));

    _ACTWait(60);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(953, 934, 800, 799, 748), 1, 0);

    soundSeDefPlay(PAL_NTSCAUG6_JUN26(1221, 1197, 1171), 0, doorUpPos, 1);

    _ACTWait(30);

    soundSeDefPlay(PAL_NTSCAUG6_JUN26(1222, 1198, 1172), 0, doorUpPos, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6_JUN26(953, 934, 800, 799, 748)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpBoyControlReadDisable = 0;

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));
}

inline void actE3St13cIntroChk(GObj *volatile self)
{
    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
    gflagOn(PAL_NTSC_AUG6_JUN26(363, 331, 330, 315));

    scpBoyControlReadDisable = 1;
    _ACTWait(120);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6_JUN26(954, 935, 801, 800, 749), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6_JUN26(954, 935, 801, 800, 749)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));

    scpBoyControlReadDisable = 0;
}

inline void actE3CageFallReadyChk(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    e3cage = 0;
    _ACTWait(1);
    while (scpTriggerFloorAttr(boyGObj, 0x2000000) == 0) {
        _ACTWait(1);
    }
    scpAdpcmPlayRequestFunc(PAL_JUN26(3, 6), &e3cage, 1, 1, 0);
}

static inline void actE3St01bEneChk_jun26(GObj *volatile self)
{
    if ((ICO_WORD)girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);

    scpBoyControlReadDisable = 1;
    _ACTWait(30);

    gflagOn(314);

    stage_SetAnimation(146, 1, 0);

    while (stage_CheckAnimationFinish(146) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(50);

    scpBoyControlReadDisable = 0;
}

inline void actE3St01bEneChk(GObj *volatile self)
{
    if (ICO_JUN26) {
        actE3St01bEneChk_jun26(self);
        return;
    }
    if ((ICO_WORD)girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));

    scpBoyControlReadDisable = 1;
    _ACTWait(30);

    gflagOn(PAL_NTSC_AUG6JUN26(362, 330, 329));

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(182, 178, 161, 160), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(182, 178, 161, 160)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));

    scpBoyControlReadDisable = 0;
}

inline void actE3St09aBrgMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = st09aBrgMain_mes;

    while (1) {
        _ACTWait(1);
    }
}

static inline void actE3St09aBrgSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    st09aBrgSwitch_mes[0].func = actE3St09aBrgDown;
    sub->mail = st09aBrgSwitch_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}
