#include "st13c.h"
#include "StageManager.h"
#include "debug.h"
#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
#include "gobj_process.h"
#include "obj_manager.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "act-game.h"
#include "act.h"
#include "commonact.h"
#include "enemy_act.h"
#include "way_llf.h"
#include "camera-ico2.h"
#include "camera-root.h"
#include "fightSound.h"
#include "generator.h"
#include "gflag.h"
#include "script.h"
#include "GsBase.h"
#include "StageAnimation.h"
#include "motionManager2.h"
#include "weapon.h"
#include <libvu0.h>
#include <string.h>
#include "e3.h"
#include "typedef.h"
#include "main.h"
#include "jimaku.h"

static void actSt13cHandSub(GObj *volatile self);

/* the stage-animation number sets the contes play; two functions keep one
   in their frame as the scratch vector they hand scpPlayMotDir or
   DirectCallEnemy. */
typedef struct AnimSet { /* field names derived */
    int anim[5];         /* 0x00 */
} AnimSet;               /* derived name */

typedef struct AnimSet16 { /* field names derived */
    int anim[16];          /* 0x00 */
} AnimSet16;               /* derived name */

/* .sbss: the flag each demo raises when it is over, and the generator the boss
   fight calls through. */
static int demoEnd;

static GObj *bossGenerator; /* derived name */

/* .data: actor mail packets. */

void actSt13cSleep(GObj *volatile self);
void actSt13cCageDown(GObj *volatile self);
void actSt13cCageFallReady(GObj *volatile self);
void actSt13cEnemy(GObj *volatile self);
void actSt13cEnemyNull(GObj *volatile self);
void actSt13cSekizo(GObj *volatile self);
void actSt13cSekizoJimaku(GObj *volatile self);
void actSt13cHand(GObj *volatile self);
void actSt13cGirlCarry(GObj *volatile self);
void actSt13cRescue(GObj *volatile self);
void actSt13cBuki(GObj *volatile self);
void actSt13cBmg1Event(int x);
void actSt13cSleepEvent(int x);
void actSt13cSleepChk(GObj *volatile self);
void actSt13cCageDownMain(GObj *volatile self);
void actSt13cCageDownSwitch(GObj *volatile self);
void actSt13cCage1stDownDemo(GObj *volatile self);
void actSt13cCageFallReadyChk(GObj *volatile self);
void actSt13cCageFallEvent(int x);
static void actE3St13cSekizoEvent(int x);
void actSt13cSekizoJimakuChk(GObj *volatile self);
void actSt13cSekizoJimakuEff(GObj *volatile self);
void actSt13cGirlCarryAgainChk(GObj *volatile self);
void actSt13cRescueChk(GObj *volatile self);
void actSt13cBukiEvent(int x);
void actSt13cBukiChk(GObj *volatile self);

/* .data: actor mail packets. */

static ActMail bmg1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail bmg1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail bmg1_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define bmg1_mes PAL_NTSC_AUG6JUN26(bmg1_mes_pal, bmg1_mes_ntsc, bmg1_mes_aug6jun26)

static ActMail sleep_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail sleep_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail sleep_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define sleep_mes PAL_NTSC_AUG6JUN26(sleep_mes_pal, sleep_mes_ntsc, sleep_mes_aug6jun26)

static ActMail cageDownMain_mes_pal[2] = {{406, actSt13cCageDownSwitch}, {429}}; /* derived name */

static ActMail cageDownMain_mes_ntsc[2] = {{376, actSt13cCageDownSwitch}, {396}}; /* derived name */

static ActMail cageDownMain_mes_aug6jun26[2] = {{372, actSt13cCageDownSwitch},
                                                {392}}; /* derived name */

#define cageDownMain_mes                                                                           \
    PAL_NTSC_AUG6JUN26(cageDownMain_mes_pal, cageDownMain_mes_ntsc, cageDownMain_mes_aug6jun26)

static ActMail cageDown_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail cageDown_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail cageDown_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define cageDown_mes PAL_NTSC_AUG6JUN26(cageDown_mes_pal, cageDown_mes_ntsc, cageDown_mes_aug6jun26)

static ActMail cageDownSwitch_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail cageDownSwitch_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail cageDownSwitch_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define cageDownSwitch_mes                                                                         \
    PAL_NTSC_AUG6JUN26(cageDownSwitch_mes_pal, cageDownSwitch_mes_ntsc,                            \
                       cageDownSwitch_mes_aug6jun26)

static ActMail cageFallReady_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail cageFallReady_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail cageFallReady_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define cageFallReady_mes                                                                          \
    PAL_NTSC_AUG6JUN26(cageFallReady_mes_pal, cageFallReady_mes_ntsc, cageFallReady_mes_aug6jun26)

static ActMail cageFall_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail cageFall_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail cageFall_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define cageFall_mes PAL_NTSC_AUG6JUN26(cageFall_mes_pal, cageFall_mes_ntsc, cageFall_mes_aug6jun26)

static ActMail cageFall2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail cageFall2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail cageFall2_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define cageFall2_mes                                                                              \
    PAL_NTSC_AUG6JUN26(cageFall2_mes_pal, cageFall2_mes_ntsc, cageFall2_mes_aug6jun26)

static ActMail sekizoJimaku_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail sekizoJimaku_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail sekizoJimaku_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define sekizoJimaku_mes                                                                           \
    PAL_NTSC_AUG6JUN26(sekizoJimaku_mes_pal, sekizoJimaku_mes_ntsc, sekizoJimaku_mes_aug6jun26)

static ActMail sekizo_mes[2] = {{430}, {429}}; /* derived name */

static ActMail girlCarry_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail girlCarry_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail girlCarry_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define girlCarry_mes                                                                              \
    PAL_NTSC_AUG6JUN26(girlCarry_mes_pal, girlCarry_mes_ntsc, girlCarry_mes_aug6jun26)

static ActMail girlCarryChk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail girlCarryAgainChk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail girlCarryAgainChk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail girlCarryAgainChk_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define girlCarryAgainChk_mes                                                                      \
    PAL_NTSC_AUG6JUN26(girlCarryAgainChk_mes_pal, girlCarryAgainChk_mes_ntsc,                      \
                       girlCarryAgainChk_mes_aug6jun26)

static ActMail hand_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail hand_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail hand_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define hand_mes PAL_NTSC_AUG6JUN26(hand_mes_pal, hand_mes_ntsc, hand_mes_aug6jun26)

static ActMail rescue_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail rescue_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail rescue_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define rescue_mes PAL_NTSC_AUG6JUN26(rescue_mes_pal, rescue_mes_ntsc, rescue_mes_aug6jun26)

static ActMail buki_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail buki_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail buki_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define buki_mes PAL_NTSC_AUG6JUN26(buki_mes_pal, buki_mes_ntsc, buki_mes_aug6jun26)

/* .sdata: bmg and hand around the two scene streams, then a word no code
   uses, a shake and its volume. */
SqEntry *bmg = 0;

static SqEntry *st13c_adpcm = 0; /* derived name */

static SqEntry *st13c_adpcm2 = 0; /* derived name */

SqEntry *hand = 0;

static SqEntry *st13c_reserved = 0; /* derived name */

static int st13c_yure = 0; /* derived name */

static unsigned char st13c_yure_vol = 0; /* derived name */

void actSt13cInit(void)
{
    if (gflagChk(PAL_NTSCAUG6JUN26(21, 20))) {
        SetWayGroupActive(9, 0);
    }
}

static void actSt13cEnd_ntsc(void)
{
    if (gflagChk(29) == 0) {
        debug_StdPrintfDummy("BackStageOff\n");
        gflagOn(357);
    }
}

static void actSt13cEnd_aug6jun26(void)
{
    if (gflagChk(29) == 0) {
        gflagOn(356);
    } else {
        gflagOff(356);
    }
}

void actSt13cEnd(void)
{
    if (ICO_NTSC) {
        actSt13cEnd_ntsc();
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cEnd_aug6jun26();
        return;
    }
    if (gflagChk(31) == 0) {
        debug_StdPrintfDummy("BackStageOff\n");
        gflagOn(390);
    }
}

/* A 16-byte constant vector template: the float view carries the values,
   the long long view is the one the copy reads. */
typedef union { /* field names derived */
    float f[4];
    long long d[2];
} __attribute__((aligned(16))) ConstVecSt13c; /* derived name */

/* The animations actSt13cConte04 steps through (the Japanese disc's 4 lower). */
static const AnimSet conte04Anims_pal = {{625, 626, 627, 628, 629}}; /* derived name */

static const AnimSet conte04Anims_jpn = {{621, 622, 623, 624, 625}}; /* derived name */

#define conte04Anims (*PAL_JPNKORCN(&conte04Anims_pal, &conte04Anims_jpn))

/* Where actSt13cSleepChk turns the sleeping girl to face. */
static const ConstVecSt13c sleepFacePos = {{-800.0f, 0.0f, -1000.0f, 1.0f}}; /* derived name */

/* The animations actSt13cConte05 steps through (the Japanese disc's 4 lower). */
static const AnimSet16 conte05Anims_pal = {
    /* derived name */
    {630, 631, 632, 633, 634, 635, 636, 637, 638, 639, 640, 641, 642, 643, 644, 645}};

static const AnimSet16 conte05Anims_jpn = {
    /* derived name */
    {626, 627, 628, 629, 630, 631, 632, 633, 634, 635, 636, 637, 638, 639, 640, 641}};

#define conte05Anims (*PAL_JPNKORCN(&conte05Anims_pal, &conte05Anims_jpn))

/* actSt13cCageFallEffect's nine effect spawns, in the frame order it fires them. */
static const EffectArg cageFallEffect1 = {{-88.0f, -50.0f, -1.0f, 1.0f}}; /* derived name */

static const EffectArg cageFallEffect2 = {{-96.0f, -45.0f, 34.0f, 1.0f}}; /* derived name */

static const EffectArg cageFallEffect3 = {{72.0f, -50.0f, 8.0f, 1.0f}}; /* derived name */

static const EffectArg cageFallEffect4 = {{80.0f, -50.0f, 2.0f, 1.0f}}; /* derived name */

static const EffectArg cageFallEffect5 = {{-27.0f, -50.0f, 100.0f, 1.0f}}; /* derived name */

static const EffectArg cageFallEffect6 = {{-56.0f, -50.0f, 66.0f, 1.0f}}; /* derived name */

static const EffectArg cageFallEffect7 = {{-5.0f, -50.0f, 42.0f, 1.0f}}; /* derived name */

static const EffectArg cageFallEffect8 = {{-10.0f, 0.0f, 466.0f, 1.0f}}; /* derived name */

static const EffectArg cageFallEffect9 = {{-25.0f, 0.0f, 450.0f, 1.0f}}; /* derived name */

static void actSt13cBmg1_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (girlGObj == 0) {
        return;
    }
    if (gflagChk(20) != 0) {
        return;
    }

    if (gflagChk(19) != 0) {
        scpPlayPosSet(girlGObj, -30.0f, -436.0f, -1.0f);
        scpPlayStart(girlGObj);
    } else if (gflagChk(17) == 0) {
        ScpCallCameraOff();
        scpPlayPosSet(girlGObj, -7.0f, -5725.0f, 18.0f);

        bmg1_mes[0].func = actSt13cBmg1Chk;
        act->mail = bmg1_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        scpPlayPosSet(girlGObj, -7.0f, -5725.0f, 18.0f);
    }
}

static void actSt13cBmg1_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (girlGObj == 0) {
        return;
    }
    if (gflagChk(20) != 0) {
        return;
    }

    if (gflagChk(19) != 0) {
        scpPlayPosSet(girlGObj, -30.0f, -436.0f, -1.0f);
        scpPlayStart(girlGObj);
    } else if (gflagChk(17) == 0) {
        ScpCallCameraOff();
        scpPlayPosSet(girlGObj, -7.0f, -5725.0f, 18.0f);

        bmg1_mes[0].func = actSt13cBmg1Chk;
        act->mail = bmg1_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    } else {
        scpPlayPosSet(girlGObj, -7.0f, -5725.0f, 18.0f);
    }
}

void actSt13cBmg1(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cBmg1_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cBmg1_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (girlGObj == 0) {
        return;
    }
    if (gflagChk(21) != 0) {
        return;
    }

    GOBJ_ACT(boyGObj)->flags |= 0x100000;

    if (gflagChk(20) != 0) {
        scpPlayPosSet(girlGObj, -30.0f, -436.0f, -1.0f);
        _ACTWait(60);
        scpPlayStart(girlGObj);
    } else if (gflagChk(18) == 0) {
        ScpCallCameraOff();
        scpPlayPosSet(girlGObj, -7.0f, -5725.0f, 18.0f);

        bmg1_mes[0].func = actSt13cBmg1Chk;
        act->mail = bmg1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        scpPlayPosSet(girlGObj, -7.0f, -5725.0f, 18.0f);
    }
}

void actSt13cBmg1Chk(GObj *volatile self)
{
    GProc *th1;
    GProc *th2;
    unsigned int i;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    ScpCallCameraOn();
    if (ICO_NTSCAUG6JUN26) {
        lt_switch_layout(51);
        gflagOn(17);
    } else {
        lt_switch_layout(PAL_JPNKORCN(55, 52));
        gflagOn(18);
    }

    scpAdpcmPlayRequestFunc(13, &bmg, 1, 1, 1);

    while (bmg == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    _ACTWait(1);

    th1 = actCreateSubThread(actSt13cConte04, 21);
    th2 = actCreateSubThread(actSt13cConte04Jimaku, 21);

    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    if (demoEnd == 0) {
        if (ICO_NTSC) {
            scpAdpcmFadeClose(bmg, 256);
        } else if (ICO_AUG6JUN26) {
            scpAdpcmFadeClose(bmg, 128);
        } else {
            scpAdpcmFadeCloseFunc(&bmg, 256);
        }

        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }

        iosThreadSetPri(&((GProc *)th1)->thread, 34);
        iosThreadSetPri(&((GProc *)th2)->thread, 34);

        {
            AnimSet w = conte04Anims;

            for (i = 0; i < 5; i++) {
                stage_SetAnimation(w.anim[i], 1, -1);
                _ACTWait(1);
            }
        }

        jimakuUndisp(&jimaku_msg);

        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(629, 625, 558, 557), 1, -1);
        scpFadeIn(3.0f);
    } else {
        iosThreadSetPri(&((GProc *)th1)->thread, 34);
        iosThreadSetPri(&((GProc *)th2)->thread, 34);
    }

    scpPlayMot(boyGObj, 0);

    {
        sceVu0FVECTOR dir;

        sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
        scpPlayMotDir(boyGObj, dir);
    }
    scpPlayEnd(boyGObj);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));
}

static void actSt13cConte04_ntsc(GObj *volatile self)
{
    scpPlayStart(boyGObj);

    stage_SetAnimation(554, 1, 0);

    scpPlayMot(boyGObj, 302);
    while (stage_ContinueAnimation(554, 555) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 303);
    while (stage_ContinueAnimation(555, 556) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 304);
    while (stage_ContinueAnimation(556, 557) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 305);
    while (stage_ContinueAnimation(557, 558) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 306);
    while (stage_CheckAnimationFinish(558) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

static void actSt13cConte04_aug6jun26(GObj *volatile self)
{
    scpPlayStart(boyGObj);

    stage_SetAnimation(553, 1, 0);

    scpPlayMot(boyGObj, 302);
    while (stage_ContinueAnimation(553, 554) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 303);
    while (stage_ContinueAnimation(554, 555) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 304);
    while (stage_ContinueAnimation(555, 556) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 305);
    while (stage_ContinueAnimation(556, 557) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 306);
    while (stage_CheckAnimationFinish(557) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

void actSt13cConte04(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cConte04_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cConte04_aug6jun26(self);
        return;
    }
    scpPlayStart(boyGObj);

    stage_SetAnimation(PAL_JPNKORCN(625, 621), 1, 0);

    scpPlayMot(boyGObj, 308);
    while (stage_ContinueAnimation(PAL_JPNKORCN(625, 621), PAL_JPNKORCN(626, 622)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 309);
    while (stage_ContinueAnimation(PAL_JPNKORCN(626, 622), PAL_JPNKORCN(627, 623)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 310);
    while (stage_ContinueAnimation(PAL_JPNKORCN(627, 623), PAL_JPNKORCN(628, 624)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 311);
    while (stage_ContinueAnimation(PAL_JPNKORCN(628, 624), PAL_JPNKORCN(629, 625)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 312);
    while (stage_CheckAnimationFinish(PAL_JPNKORCN(629, 625)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

void actSt13cConte04Jimaku(GObj *volatile self)
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
        case 290:
            jimaku_msg.sub.block = 2;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 800:
            jimaku_msg.sub.block = 4;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 1070:
            jimaku_msg.sub.block = 5;
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
    } while (t < 1400.0f);
    _ACTWait(0);
}

static void actSt13cCage1stDownDemoCancel_ntscaug6jun26(GObj *volatile self)
{
    float ofs[4];
    float dir[4];
    GProc *proc;

    demoEnd = 0;

    proc = (GProc *)actCreateSubThread(actSt13cCage1stDownDemo, 21);

    while (demoEnd == 0) {
        _ACTWait(1);
    }

    iosThreadSetPri(&proc->thread, 34);

    scpPlayMot(boyGObj, 308);
    scpPlayWaitMotEnd(boyGObj);

    while (stage_CheckAnimationFrame(60, 0, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);

    memset(ofs, 0, 16);
    ofs[3] = 1.0f;
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    scpPlayEnd(boyGObj);

    CameraSetCameraSet(37);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actSt13cCage1stDownDemoCancel(GObj *volatile self)
{
    if (ICO_NTSCAUG6JUN26) {
        actSt13cCage1stDownDemoCancel_ntscaug6jun26(self);
        return;
    }
    float ofs[4];
    float dir[4];
    IOSThread *th;

    demoEnd = 0;

    th = &((GProc *)actCreateSubThread(actSt13cCage1stDownDemo, 21))->thread;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(th, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }
    }

    scpPlayMot(boyGObj, 314);
    scpPlayWaitMotEnd(boyGObj);

    if (demoEnd == 0) {
        scpFadeIn(3.0f);
    }

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(73, 69), 0, 1) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);

    memset(ofs, 0, 16);
    ofs[3] = 1.0f;
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    scpPlayEnd(boyGObj);

    CameraSetCameraSet(37);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

static void actSt13cCage1stDown_aug6jun26(GObj *volatile self)
{
    int se;

    lt_switch_layout(51);
    gflagOn(19);

    scpAdpcmPlayRequestFunc(14, &st13c_adpcm, 1, 1, 1);

    while (st13c_adpcm == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(59, 1, 0);
    stage_SetAnimation(60, 1, 0);

    actCreateSubThread(actSt13cCage1stDownDemoCancel, 21);

    scpPlayStart(girlGObj);

    while (stage_CheckAnimationFrame(59, 15, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMotReq(girlGObj, 252);

    while (stage_CheckAnimationFrame(59, 45, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    se = soundSeDefPlay(1328, 0, 0, 1);

    while (stage_CheckAnimationFinish(59) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    soundSeDefStop(se);

    while (stage_CheckAnimationFinish(59) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpPlayEnd(girlGObj);
}

static void actSt13cCage1stDown_ntsc(GObj *volatile self)
{
    int se;

    lt_switch_layout(51);
    gflagOn(19);

    scpAdpcmPlayRequestFunc(14, &st13c_adpcm, 1, 1, 1);

    while (st13c_adpcm == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(59, 1, 0);
    stage_SetAnimation(60, 1, 0);

    actCreateSubThread(actSt13cCage1stDownDemoCancel, 21);

    scpPlayStart(girlGObj);

    while (stage_CheckAnimationFrame(59, 15, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMotReq(girlGObj, 254);

    while (stage_CheckAnimationFrame(59, 45, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    se = soundSeDefPlay(1328, 0, 0, 1);

    while (stage_CheckAnimationFinish(59) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    soundSeDefStop(se);

    while (stage_CheckAnimationFinish(59) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpPlayEnd(girlGObj);
}

void actSt13cCage1stDown(GObj *volatile self)
{
    if (ICO_AUG6JUN26) {
        actSt13cCage1stDown_aug6jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt13cCage1stDown_ntsc(self);
        return;
    }
    int se;

    if (ICO_NTSCAUG6JUN26) {
        lt_switch_layout(51);
        gflagOn(19);
    } else {
        lt_switch_layout(PAL_JPNKORCN(55, 52));
        gflagOn(20);
    }

    scpAdpcmPlayRequestFunc(14, &st13c_adpcm, 1, 1, 1);

    while (st13c_adpcm == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSCAUG6JUN26) {
        stage_SetAnimation(59, 1, 0);
        stage_SetAnimation(60, 1, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(72, 68), 1, 0);
        stage_SetAnimation(PAL_JPNKORCN(73, 69), 1, 0);
    }

    actCreateSubThread(actSt13cCage1stDownDemoCancel, 21);

    scpPlayStart(girlGObj);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSCAUG6JUN26(72, 68, 59), 15, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMotReq(girlGObj, PAL_NTSC_AUG6JUN26(286, 254, 252));

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSCAUG6JUN26(72, 68, 59), 45, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    se = soundSeDefPlay(PAL_NTSCAUG6JUN26(1355, 1328), 0, 0, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSCAUG6JUN26(72, 68, 59)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    soundSeDefStop(se);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSCAUG6JUN26(72, 68, 59)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
}

void actSt13cCageFall(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(20, 19)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSCAUG6JUN26(72, 68, 59), -1, -2);

        scpSearchGobj(PAL_NTSC_AUG6JUN26(128, 111, 110))->active = 0;
        SetWeaponTorchChainReactionFlagAll(1);

        cageFall_mes[0].func = actSt13cCageFallChk;
        act->mail = cageFall_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        if (gflagChk(PAL_NTSCAUG6JUN26(22, 21)) == 0) {
            scpSearchGobj(PAL_NTSC_AUG6JUN26(128, 111, 110))->active = 0;
            SetWeaponTorchChainReactionFlagAll(1);

            stage_SetAnimation(PAL_JPNKORCN_NTSCAUG6JUN26(72, 68, 59), 0, -1);
            stage_SetAnimation(PAL_JPNKORCN_NTSCAUG6JUN26(76, 72, 63), 0, 0);

            cageFall2_mes[0].func = actSt13cCageFallChk;
            act->mail = cageFall2_mes;
            ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
            _ACTWait(0);
        }

        scpSearchGobj(PAL_NTSC_AUG6JUN26(144, 127, 126))->active = 0;

        stage_SetAnimation(PAL_JPNKORCN_NTSCAUG6JUN26(76, 72, 63), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN_NTSCAUG6JUN26(74, 70, 61), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN_NTSCAUG6JUN26(75, 71, 62), 0, -1);
    }
}

static void actSt13cCageFallChk_ntsc(GObj *volatile self)
{
    GProc *th1;
    GProc *th2;
    GProc *th3;
    int cancel;
    unsigned int i;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 160.0f) == 0 || gflagChk(19) == 0) {
        _ACTWait(1);
    }

    gflagOn(348);
    CheckPoint();
    gflagOff(348);

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    fightSoundProcessRequestPause();
    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    gflagOn(20);

    while (st13c_adpcm2 == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(st13c_adpcm2->stream);

    th1 = actCreateSubThread(actSt13cConte05, 21);
    th2 = actCreateSubThread(actSt13cConte05Jimaku, 21);
    th3 = actCreateSubThread(actSt13cCageFallEffect, 21);

    SetWayGroupActive(9, 0);
    _ACTWait(1);

    stage_SetAnimation(61, 1, 0);
    stage_SetAnimation(62, 1, 0);
    stage_SetAnimation(63, 1, 0);

    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    cancel = demoEnd ^ 1;

    if (cancel) {
        scpAdpcmFadeClose(st13c_adpcm2, 256);

        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    }

    iosThreadSetPri(&((GProc *)th1)->thread, 34);
    iosThreadSetPri(&((GProc *)th2)->thread, 34);
    iosThreadSetPri(&((GProc *)th3)->thread, 34);

    if (cancel) {
        {
            AnimSet16 w = conte05Anims;

            for (i = 0; i < 16; i++) {
                stage_SetAnimation(w.anim[i], 1, -1);
                _ACTWait(1);
            }
        }

        jimakuUndisp(&jimaku_msg);

        scpSearchGobj(111)->active = 1;
        scpSearchGobj(112)->active = 1;
        scpSearchGobj(113)->active = 1;
        scpSearchGobj(51)->active = 1;

        scpTorchLightOn(127);
        gflagOn(22);
        _ACTWait(10);

        Generator_QuickCall(bossGenerator);
        Generator_MaskOff(bossGenerator);

        if (isEnemyActive(scpSearchGobj(133)) == 0) {
            sceVu0FVECTOR zero;

            memset(zero, 0, 16);
            DirectCallEnemy(scpSearchGobj(133), bossGenerator, zero, zero, 0);
            iosOmSendMail(scpSearchGobj(133), 242, scpSearchGobj(133));
            _ACTWait(1);
        }

        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayStart(scpSearchGobj(133));

        fightSoundProcessRequestStart();

        stage_SetAnimation(61, 0, -1);
        stage_SetAnimation(62, 0, -1);
        stage_SetAnimation(63, 0, -1);
        stage_SetAnimation(576, 1, 0);

        scpPlayMot(scpSearchGobj(133), 887);
        SetCameraFlag_LwsCutBack();
        scpPlayEnd(scpSearchGobj(133));
        scpWakeupEnemyAll();

        _ACTWait(10);

        scpPlayMot(boyGObj, 317);
        stage_SetAnimation(575, 1, 0);
        scpTorchLightOff(127);
        scpFadeIn(3.0f);
    } else {
        Generator_MaskOff(bossGenerator);
    }

    scpPlayMot(girlGObj, 691);
    scpPlayMot(scpSearchGobj(133), 919);

    while (stage_ContinueAnimation(575, 576) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 318);
    scpPlayMot(scpSearchGobj(133), 887);

    while (stage_CheckAnimationFinish(576) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(scpSearchGobj(133));
    scpWakeupEnemyAll();
    ACTEnemyForceSwitchToCarry(scpSearchGobj(133));
    scpTorchLightOff(127);

    scpSearchGobj(127)->active = 0;

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);

    _ACTWait(30);
    gflagOn(24);
    SetWeaponTorchChainReactionFlagAll(0);
}

static void actSt13cCageFallChk_aug6jun26(GObj *volatile self)
{
    GProc *th1;
    GProc *th2;
    GProc *th3;
    int cancel;
    unsigned int i;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 160.0f) == 0 || gflagChk(19) == 0) {
        _ACTWait(1);
    }

    gflagOn(347);
    CheckPoint();
    gflagOff(347);

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    fightSoundProcessRequestPause();
    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    gflagOn(20);

    while (st13c_adpcm2 == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(st13c_adpcm2->stream);

    th1 = actCreateSubThread(actSt13cConte05, 21);
    stage_SetAnimation(61, 1, 0);
    stage_SetAnimation(62, 1, 0);
    stage_SetAnimation(63, 1, 0);
    th2 = actCreateSubThread(actSt13cConte05Jimaku, 21);
    th3 = actCreateSubThread(actSt13cCageFallEffect, 21);

    SetWayGroupActive(9, 0);

    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    cancel = demoEnd ^ 1;

    if (cancel) {
        scpAdpcmFadeClose(st13c_adpcm2, 192);

        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    }

    iosThreadSetPri(&((GProc *)th1)->thread, 34);
    iosThreadSetPri(&((GProc *)th2)->thread, 34);
    iosThreadSetPri(&((GProc *)th3)->thread, 34);

    if (cancel) {
        {
            AnimSet16 w = conte05Anims;

            for (i = 0; i < 16; i++) {
                stage_SetAnimation(w.anim[i], 1, -1);
                _ACTWait(1);
            }
        }

        jimakuUndisp(&jimaku_msg);

        scpSearchGobj(110)->active = 1;
        scpSearchGobj(111)->active = 1;
        scpSearchGobj(112)->active = 1;
        scpSearchGobj(50)->active = 1;

        scpTorchLightOn(126);
        gflagOn(22);
        _ACTWait(10);

        Generator_QuickCall(bossGenerator);
        Generator_MaskOff(bossGenerator);

        if (isEnemyActive(scpSearchGobj(132)) == 0) {
            sceVu0FVECTOR zero;

            memset(zero, 0, 16);
            DirectCallEnemy(scpSearchGobj(132), bossGenerator, zero, zero, 0);
            iosOmSendMail(scpSearchGobj(132), 239, scpSearchGobj(132));
            _ACTWait(1);
        }

        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayStart(scpSearchGobj(132));

        fightSoundProcessRequestStart();

        stage_SetAnimation(61, 0, -1);
        stage_SetAnimation(62, 0, -1);
        stage_SetAnimation(63, 0, -1);
        stage_SetAnimation(575, 1, 0);

        scpPlayMot(scpSearchGobj(132), 889);
        SetCameraFlag_LwsCutBack();
        scpPlayEnd(scpSearchGobj(132));
        scpWakeupEnemyAll();

        _ACTWait(10);

        scpPlayMot(boyGObj, 317);
        stage_SetAnimation(574, 1, 0);
        scpTorchLightOff(126);
        scpFadeIn(3.0f);
    }

    scpPlayMot(girlGObj, 693);
    scpPlayMot(scpSearchGobj(132), 921);

    while (stage_ContinueAnimation(574, 575) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 318);
    scpPlayMot(scpSearchGobj(132), 889);

    while (stage_CheckAnimationFinish(575) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(scpSearchGobj(132));
    scpWakeupEnemyAll();
    ACTEnemyForceSwitchToCarry(scpSearchGobj(132));
    scpTorchLightOff(126);

    scpSearchGobj(126)->active = 0;

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);

    _ACTWait(30);
    gflagOn(24);
    SetWeaponTorchChainReactionFlagAll(0);
}

void actSt13cCageFallChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cCageFallChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cCageFallChk_aug6jun26(self);
        return;
    }
    GProc *th1;
    GProc *th2;
    GProc *th3;
    int cancel;
    unsigned int i;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 160.0f) == 0 || gflagChk(20) == 0) {
        _ACTWait(1);
    }

    gflagOn(381);
    CheckPoint();
    gflagOff(381);

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;

    fightSoundProcessRequestPause();
    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    gflagOn(21);

    while (st13c_adpcm2 == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(st13c_adpcm2->stream);

    th1 = actCreateSubThread(actSt13cConte05, 21);
    th2 = actCreateSubThread(actSt13cConte05Jimaku, 21);
    th3 = actCreateSubThread(actSt13cCageFallEffect, 21);

    SetWayGroupActive(9, 0);
    _ACTWait(1);

    stage_SetAnimation(PAL_JPNKORCN(74, 70), 1, 0);
    stage_SetAnimation(PAL_JPNKORCN(75, 71), 1, 0);
    stage_SetAnimation(PAL_JPNKORCN(76, 72), 1, 0);

    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    cancel = demoEnd ^ 1;

    if (cancel) {
        scpAdpcmFadeCloseFunc(&st13c_adpcm2, 256);

        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    }

    iosThreadSetPri(&((GProc *)th1)->thread, 34);
    iosThreadSetPri(&((GProc *)th2)->thread, 34);
    iosThreadSetPri(&((GProc *)th3)->thread, 34);

    if (cancel) {
        {
            AnimSet16 w = conte05Anims;

            for (i = 0; i < 16; i++) {
                stage_SetAnimation(w.anim[i], 1, -1);
                _ACTWait(1);
            }
        }

        jimakuUndisp(&jimaku_msg);

        scpSearchGobj(128)->active = 1;
        scpSearchGobj(129)->active = 1;
        scpSearchGobj(130)->active = 1;
        scpSearchGobj(54)->active = 1;

        scpTorchLightOn(144);
        ResetHandCameraLimitInDemo();
        gflagOn(23);
        _ACTWait(10);

        Generator_QuickCall(bossGenerator);
        Generator_MaskOff(bossGenerator);

        if (isEnemyActive(scpSearchGobj(150)) == 0) {
            sceVu0FVECTOR zero;

            memset(zero, 0, 16);
            DirectCallEnemy(scpSearchGobj(150), bossGenerator, zero, zero, 0);
            iosOmSendMail(scpSearchGobj(150), 258, scpSearchGobj(150));
            _ACTWait(1);
        }

        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayStart(scpSearchGobj(150));

        fightSoundProcessRequestStart();

        stage_SetAnimation(PAL_JPNKORCN(74, 70), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(75, 71), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(76, 72), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(647, 643), 1, 0);

        scpPlayMot(scpSearchGobj(150), 937);
        SetCameraFlag_LwsCutBack();
        scpPlayEnd(scpSearchGobj(150));
        scpWakeupEnemyAll();

        _ACTWait(10);

        scpPlayMot(boyGObj, 323);
        stage_SetAnimation(PAL_JPNKORCN(646, 642), 1, 0);
        scpTorchLightOff(144);
        scpFadeIn(3.0f);
    } else {
        Generator_MaskOff(bossGenerator);
    }

    scpPlayMot(girlGObj, 736);
    scpPlayStart(scpSearchGobj(150));
    scpPlayMot(scpSearchGobj(150), 968);

    while (stage_ContinueAnimation(PAL_JPNKORCN(646, 642), PAL_JPNKORCN(647, 643)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 324);
    scpPlayMot(scpSearchGobj(150), 937);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(647, 643)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(scpSearchGobj(150));
    scpWakeupEnemyAll();
    ACTEnemyForceSwitchToCarry(scpSearchGobj(150));
    scpTorchLightOff(144);

    scpSearchGobj(144)->active = 0;

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);

    GOBJ_ACT(scpSearchGobj(150))->flags20.ll |= 0x20000;

    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));

    _ACTWait(30);
    gflagOn(25);
    SetWeaponTorchChainReactionFlagAll(0);

    GOBJ_ACT(boyGObj)->flags &= ~0x100000;
}

static void actSt13cConte05_ntsc(GObj *volatile self)
{
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

    scpPlayMot(boyGObj, 311);

    while (stage_ContinueAnimation(564, 565) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(girlGObj, 683);
    _ACTWait(1);

    scpSearchGobj(111)->active = 1;
    scpSearchGobj(112)->active = 0;
    scpSearchGobj(113)->active = 0;
    scpSearchGobj(51)->active = 0;

    while (stage_ContinueAnimation(565, 566) == 0) {
        _ACTWait(1);
    }

    scpTorchLightOn(127);

    scpPlayMot(girlGObj, 684);

    while (stage_ContinueAnimation(566, 567) == 0) {
        _ACTWait(1);
    }

    scpSearchGobj(51)->active = 1;

    scpPlayMot(boyGObj, 312);
    scpPlayMot(girlGObj, 685);

    while (stage_ContinueAnimation(567, 568) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 313);
    scpPlayMot(girlGObj, 686);

    scpSearchGobj(112)->active = 1;
    scpSearchGobj(113)->active = 1;

    _ACTWait(300);
    gflagOn(22);

    while (stage_ContinueAnimation(568, 569) == 0) {
        _ACTWait(1);
    }

    scpPlayStart(scpSearchGobj(133));
    scpPlayMot(scpSearchGobj(133), 915);

    while (stage_ContinueAnimation(569, 570) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 314);
    scpPlayMot(girlGObj, 687);
    scpPlayMot(scpSearchGobj(133), 915);

    while (stage_ContinueAnimation(570, 571) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 315);
    scpPlayMot(girlGObj, 688);
    scpPlayMot(scpSearchGobj(133), 916);

    while (stage_ContinueAnimation(571, 572) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 316);
    scpPlayMot(girlGObj, 689);
    scpPlayMot(scpSearchGobj(133), 917);

    while (stage_ContinueAnimation(572, 573) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(girlGObj, 690);
    scpPlayMot(scpSearchGobj(133), 918);

    while (stage_ContinueAnimation(573, 574) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 317);

    while (stage_ContinueAnimation(574, 575) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(girlGObj, 691);
    scpPlayMot(scpSearchGobj(133), 919);

    fightSoundProcessRequestStart();

    demoEnd = 1;
    _ACTWait(0);
}

static void actSt13cConte05_aug6jun26(GObj *volatile self)
{
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

    scpPlayMot(boyGObj, 311);

    while (stage_ContinueAnimation(563, 564) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(girlGObj, 685);
    _ACTWait(1);

    scpSearchGobj(110)->active = 1;
    scpSearchGobj(111)->active = 0;
    scpSearchGobj(112)->active = 0;
    scpSearchGobj(50)->active = 0;

    while (stage_ContinueAnimation(564, 565) == 0) {
        _ACTWait(1);
    }

    scpTorchLightOn(126);

    scpPlayMot(girlGObj, 686);

    while (stage_ContinueAnimation(565, 566) == 0) {
        _ACTWait(1);
    }

    scpSearchGobj(50)->active = 1;

    scpPlayMot(boyGObj, 312);
    scpPlayMot(girlGObj, 687);

    while (stage_ContinueAnimation(566, 567) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 313);
    scpPlayMot(girlGObj, 688);

    scpSearchGobj(111)->active = 1;
    scpSearchGobj(112)->active = 1;

    _ACTWait(300);
    gflagOn(22);

    while (stage_ContinueAnimation(567, 568) == 0) {
        _ACTWait(1);
    }

    scpPlayStart(scpSearchGobj(132));
    scpPlayMot(scpSearchGobj(132), 917);

    while (stage_ContinueAnimation(568, 569) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 314);
    scpPlayMot(girlGObj, 689);
    scpPlayMot(scpSearchGobj(132), 917);

    while (stage_ContinueAnimation(569, 570) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 315);
    scpPlayMot(girlGObj, 690);
    scpPlayMot(scpSearchGobj(132), 918);

    while (stage_ContinueAnimation(570, 571) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 316);
    scpPlayMot(girlGObj, 691);
    scpPlayMot(scpSearchGobj(132), 919);

    while (stage_ContinueAnimation(571, 572) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(girlGObj, 692);
    scpPlayMot(scpSearchGobj(132), 920);

    while (stage_ContinueAnimation(572, 573) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 317);

    while (stage_ContinueAnimation(573, 574) == 0) {
        _ACTWait(1);
    }

    fightSoundProcessRequestStart();

    demoEnd = 1;
    _ACTWait(0);
}

void actSt13cConte05(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cConte05_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cConte05_aug6jun26(self);
        return;
    }
    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);

    stage_SetAnimation(PAL_JPNKORCN(630, 626), 1, 0);

    scpPlayMot(boyGObj, 315);
    scpPlayMot(girlGObj, 726);

    while (stage_ContinueAnimation(PAL_JPNKORCN(630, 626), PAL_JPNKORCN(631, 627)) == 0) {
        _ACTWait(1);
    }

    SetHandCameraLimitInDemo(0, 0);
    scpPlayMot(girlGObj, 726);

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

    scpPlayMot(boyGObj, 317);

    while (stage_ContinueAnimation(PAL_JPNKORCN(635, 631), PAL_JPNKORCN(636, 632)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(girlGObj, 728);
    _ACTWait(1);

    scpSearchGobj(128)->active = 1;
    scpSearchGobj(129)->active = 0;
    scpSearchGobj(130)->active = 0;
    scpSearchGobj(54)->active = 0;

    scpTorchLightOn(144);

    while (stage_ContinueAnimation(PAL_JPNKORCN(636, 632), PAL_JPNKORCN(637, 633)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(girlGObj, 729);

    while (stage_ContinueAnimation(PAL_JPNKORCN(637, 633), PAL_JPNKORCN(638, 634)) == 0) {
        _ACTWait(1);
    }

    SetHandCameraLimitInDemo(5, 5);

    scpSearchGobj(54)->active = 1;

    scpPlayMot(boyGObj, 318);
    scpPlayMot(girlGObj, 730);

    while (stage_ContinueAnimation(PAL_JPNKORCN(638, 634), PAL_JPNKORCN(639, 635)) == 0) {
        _ACTWait(1);
    }

    ResetHandCameraLimitInDemo();

    scpPlayMot(boyGObj, 319);
    scpPlayMot(girlGObj, 731);

    scpSearchGobj(129)->active = 1;
    scpSearchGobj(130)->active = 1;

    _ACTWait(300);
    gflagOn(23);

    while (stage_ContinueAnimation(PAL_JPNKORCN(639, 635), PAL_JPNKORCN(640, 636)) == 0) {
        _ACTWait(1);
    }

    scpPlayStart(scpSearchGobj(150));
    scpPlayMot(scpSearchGobj(150), 964);

    while (stage_ContinueAnimation(PAL_JPNKORCN(640, 636), PAL_JPNKORCN(641, 637)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 320);
    scpPlayMot(girlGObj, 732);
    scpPlayMot(scpSearchGobj(150), 964);

    while (stage_ContinueAnimation(PAL_JPNKORCN(641, 637), PAL_JPNKORCN(642, 638)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 321);
    scpPlayMot(girlGObj, 733);
    scpPlayMot(scpSearchGobj(150), 965);

    while (stage_ContinueAnimation(PAL_JPNKORCN(642, 638), PAL_JPNKORCN(643, 639)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 322);
    scpPlayMot(girlGObj, 734);
    scpPlayMot(scpSearchGobj(150), 966);

    while (stage_ContinueAnimation(PAL_JPNKORCN(643, 639), PAL_JPNKORCN(644, 640)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(girlGObj, 735);
    scpPlayMot(scpSearchGobj(150), 967);

    while (stage_ContinueAnimation(PAL_JPNKORCN(644, 640), PAL_JPNKORCN(645, 641)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 323);

    while (stage_ContinueAnimation(PAL_JPNKORCN(645, 641), PAL_JPNKORCN(646, 642)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(girlGObj, 736);
    scpPlayMot(scpSearchGobj(150), 968);

    fightSoundProcessRequestStart();

    demoEnd = 1;
    _ACTWait(0);
}

void actSt13cConte05Jimaku(GObj *volatile self)
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
        case 1630:
            jimakuOn = 1;
            jimaku_msg.sub.block = 9;
            jimaku_msg.sub.jump = -1;
            jimakuJump(&jimaku_msg);
            break;
        case 2492:
            jimaku_msg.sub.block = 6;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 2760:
            jimaku_msg.sub.block = 7;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 3712:
            jimaku_msg.sub.block = 8;
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
    } while (t < 4000.0f);
    _ACTWait(0);
}

void actSt13cCageFallEffect(GObj *volatile self)
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
            b1 = cageFallEffect1;
            scpEffectStart(&b1, 0);
            b2 = cageFallEffect2;
            scpEffectStart(&b2, 0);
            break;
        case 68:
            b3 = cageFallEffect3;
            scpEffectStart(&b3, 0);
            b4 = cageFallEffect4;
            scpEffectStart(&b4, 0);
            break;
        case 96:
            b5 = cageFallEffect5;
            scpEffectStart(&b5, 0);
            b6 = cageFallEffect6;
            scpEffectStart(&b6, 0);
            b7 = cageFallEffect7;
            scpEffectStart(&b7, 0);
            break;
        case 180:
            iosPadActRequest(boyPad, 15);
            break;
        case 300:
            b8 = cageFallEffect8;
            scpEffectStart(&b8, 0);
            b9 = cageFallEffect9;
            scpEffectStart(&b9, 0);
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
    _ACTWait(0);
}

static void actSt13cSekizoChk(GObj *volatile self)
{
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0 || gflagChk(28) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    SetWayGroupActive(2, 1);

    scpAdpcmPlayRequestFunc(17, &hand, 1, 1, 1);
    while (hand == 0) {
        _ACTWait(1);
    }

    scpKillEnemyAll();
    scpMaskGeneratorAll();

    stage_SetAnimation(PAL_JPNKORCN(77, 73), 1, 0);

    st13c_yure = iosPadActRequest(boyPad, 9);
    st13c_yure_vol = 128;
    iosPadActVolumeSet(st13c_yure, 128);

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 532);
    scpPlayPosSet(boyGObj, -300.0f, -100.0f, 100.0f);
    scpPlayPosSet(girlGObj, -300.0f, -100.0f, 0.0f);
    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpBoyControlReadDisable = 1;

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpSekizouCheckPoint();

    scpPlayMot(girlGObj, 645);
    scpPlayWaitMotEnd(girlGObj);

    gflagOn(31);

    /* PC port: the original stops `volatile int
       se`, which nothing writes (st07a's twin stores soundSeDefPlay(1217)
       there first; this one has no play). On the EE it is 4(sp) of the
       96-byte frame (0x24BE14 lw a0,4(sp); the only store near it, 0x24BC14
       sw a0,0(sp), is self). This function is a thread's entry (the mail's
       main, isysGObjProcAdd, iosThreadCreateS over an iosMallocDebug stack
       that nothing clears), entered from iosThreadMain's 32-byte frame
       after only the GetThreadId syscall, so the word is whatever the heap
       block held before: indeterminate. _soundSeDefStop stops a slot only
       when (id >> 8) equals the slot's unsigned short num and its handle is
       live, so any word but a still-live handle of this exact slot is a
       no-op; the host passes -256 (slot 0, id >> 8 = -1, never a num), the
       no-op without -1's read of seSlotTbl[255] past the 48 slots. */
    soundSeDefStop(-256);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(77, 73), 180, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(st13c_yure);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(77, 73)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayMot(girlGObj, 532);
    scpPlayEnd(girlGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

static void actSt13cGirlCarryChk_ntsc(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);
    static ActMail girlCarryChk_mes[2] = {{397}, {396}}; /* derived name */

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (gflagChk(24) == 0 || GOBJ_ACT(girlGObj)->actMode == 107 ||
           GOBJ_ACT(girlGObj)->actMode == 106 || gflagChk(27) != 0) {
        _ACTWait(1);
    }

    _ACTWait(15);

    scpPlayStart(girlGObj);
    scpPlayMot(girlGObj, 569);
    scpPlayWaitMotEnd(girlGObj);
    scpPlayMot(girlGObj, 552);

    gflagOn(25);

    girlCarryChk_mes[0].func = actSt13cGirlCarryAgainChk;
    act->mail = girlCarryChk_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);

    _ACTWait(1);
}

static void actSt13cGirlCarryChk_aug6jun26(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);
    static ActMail girlCarryChk_mes[2] = {{393}, {392}}; /* derived name */

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (gflagChk(24) == 0 || GOBJ_ACT(girlGObj)->actMode == 107 ||
           GOBJ_ACT(girlGObj)->actMode == 106 || gflagChk(27) != 0) {
        _ACTWait(1);
    }

    _ACTWait(15);

    scpPlayStart(girlGObj);
    scpPlayMot(girlGObj, 570);
    scpPlayWaitMotEnd(girlGObj);
    scpPlayMot(girlGObj, 553);

    gflagOn(25);

    girlCarryChk_mes[0].func = actSt13cGirlCarryAgainChk;
    act->mail = girlCarryChk_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);

    _ACTWait(1);
}

void actSt13cGirlCarryChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cGirlCarryChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cGirlCarryChk_aug6jun26(self);
        return;
    }
    Act *act = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (gflagChk(25) == 0 || GOBJ_ACT(girlGObj)->actMode == 111 ||
           GOBJ_ACT(girlGObj)->actMode == 110 || gflagChk(29) != 0) {
        _ACTWait(1);
    }

    _ACTWait(15);

    scpPlayStart(girlGObj);
    scpPlayMot(girlGObj, 595);
    scpPlayWaitMotEnd(girlGObj);
    scpPlayMot(girlGObj, 596);

    GOBJ_SUB(girlGObj)->ctrl.blendFrames =
        (int)((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f * 30.0f);

    gflagOn(26);

    girlCarryChk_mes[0].func = actSt13cGirlCarryAgainChk;
    act->mail = girlCarryChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);

    _ACTWait(1);
}

static void actSt13cHandChk_ntsc(GObj *volatile self)
{
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (1) {
        if (scpActStatusDeathFall(boyGObj) == 0 && actEnemyFlagCheckDead(scpSearchGobj(133)) != 0 &&
            scpTriggerFloorAttr(boyGObj, 0x1000000) == 0 && gflagChk(25) != 0 &&
            scpTriggerBall(girlGObj, boyGObj, 550.0f) != 0 &&
            (GOBJ_ACT(boyGObj)->pad.trg & 8) != 0 && GOBJ_ACT(girlGObj)->actMode != 106) {
            break;
        }
        if (gflagChk(28) != 0) {
            break;
        }
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    fightSoundProcessRequestPause();

    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    gsb_SetZoom(2.0f, 1000.0f);
    gflagOn(26);
    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    sceVu0SubVector(dir, test_CURRENTROOT(boyGObj), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpLockMaxRotate(boyGObj, 6.0f);
    _SCPMoveByWay_ToChar(boyGObj, girlGObj, 0, 6, 50.0f, 30.0f);
    scpUnLockMaxRotate(boyGObj);

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    sceVu0SubVector(dir, test_CURRENTROOT(boyGObj), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpAdpcmPlayRequestFunc(16, &hand, 1, 1, 1);
    while (hand == 0) {
        _ACTWait(1);
    }

    ACTGame_ConnectHand();

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);
    scpPlayMot(boyGObj, 256);
    scpPlayMot(girlGObj, 678);

    actCreateSubThread(actSt13cHandJimaku, 21);

    _ACTWait(100);
    scpPlayWaitMotEnd(boyGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    scpPlayMot(girlGObj, 514);
    scpPlayEnd(girlGObj);

    GOBJ_SUB(boyGObj)->ctrl.blendFrames =
        (int)((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f * 30.0f);

    _ACTWait(1);

    iosOmSendMail(girlGObj, 60, boyGObj);

    gsb_SetZoom(1.0f, 1000.0f);
    lt_switch_layout(50);

    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
    fightSoundProcessRequestStart();
}

static void actSt13cHandChk_aug6jun26(GObj *volatile self)
{
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (1) {
        if (scpActStatusDeathFall(boyGObj) == 0 && actEnemyFlagCheckDead(scpSearchGobj(132)) != 0 &&
            scpTriggerFloorAttr(boyGObj, 0x1000000) == 0 && gflagChk(25) != 0 &&
            scpTriggerBall(girlGObj, boyGObj, 550.0f) != 0 &&
            (GOBJ_ACT(boyGObj)->pad.trg & 8) != 0 && GOBJ_ACT(girlGObj)->actMode != 106) {
            break;
        }
        if (gflagChk(28) != 0) {
            break;
        }
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSearchGobj(132)->active = 0;

    fightSoundProcessRequestPause();

    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    gsb_SetZoom(2.0f, 1000.0f);
    gflagOn(26);
    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    sceVu0SubVector(dir, test_CURRENTROOT(boyGObj), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    _SCPMoveByWay_ToChar(boyGObj, girlGObj, 0, 2, 50.0f, 30.0f);

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    sceVu0SubVector(dir, test_CURRENTROOT(boyGObj), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpAdpcmPlayRequestFunc(16, &st13c_reserved, 1, 1, 1);
    while (st13c_reserved == 0) {
        _ACTWait(1);
    }

    ACTGame_ConnectHand();

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);
    scpPlayMot(boyGObj, 256);
    scpPlayMot(girlGObj, 680);

    actCreateSubThread(actSt13cHandJimaku, 21);

    _ACTWait(100);
    scpPlayWaitMotEnd(boyGObj);

    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);
    scpPlayMot(girlGObj, 514);
    scpPlayEnd(girlGObj);

    GOBJ_SUB(boyGObj)->ctrl.blendFrames =
        (int)((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f * 30.0f);

    _ACTWait(1);

    iosOmSendMail(girlGObj, 59, boyGObj);

    gsb_SetZoom(1.0f, 1000.0f);
    lt_switch_layout(50);

    scpBoyControlReadDisable = 0;
    scpSearchGobj(132)->active = 1;
    fightSoundProcessRequestStart();
}

void actSt13cHandChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cHandChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cHandChk_aug6jun26(self);
        return;
    }
    float dir[4];
    GProc *th1;
    GProc *th2;
    int t;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (1) {
        if (scpActStatusDeathFall(boyGObj) == 0 && actEnemyFlagCheckDead(scpSearchGobj(150)) != 0 &&
            scpTriggerFloorAttr(boyGObj, 0x1000000) == 0 &&
            scpTriggerFloorAttr(boyGObj, 0x3000000) != 0 && gflagChk(26) != 0 &&
            scpTriggerBall(girlGObj, boyGObj, 550.0f) != 0 &&
            (GOBJ_ACT(boyGObj)->pad.trg & 8) != 0 && GOBJ_ACT(girlGObj)->actMode != 110) {
            break;
        }
        if (gflagChk(30) != 0) {
            break;
        }
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    GOBJ_ACT(scpSearchGobj(150))->flags20.ll &= ~0x20000;

    fightSoundProcessRequestPause();

    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    gsb_SetZoom(2.0f, 1000.0f);
    gflagOn(27);
    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    sceVu0SubVector(dir, test_CURRENTROOT(boyGObj), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpLockMaxRotate(boyGObj, 6.0f);
    _SCPMoveByWay_ToChar(boyGObj, girlGObj, 0, 6, 50.0f, 30.0f);
    scpUnLockMaxRotate(boyGObj);

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);
    sceVu0SubVector(dir, test_CURRENTROOT(boyGObj), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpAdpcmPlayRequestFunc(16, &st13c_reserved, 1, 1, 1);
    while (st13c_reserved == 0) {
        _ACTWait(1);
    }

    ACTGame_ConnectHand();

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);
    scpPlayMot(boyGObj, 261);
    scpPlayMot(girlGObj, 721);

    th1 = actCreateSubThread(actSt13cHandJimaku, 21);
    th2 = actCreateSubThread(actSt13cHandSub, 21);

    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&((GProc *)th2)->thread, 34);
    iosThreadSetPri(&((GProc *)th1)->thread, 34);

    if (demoEnd == 0) {
        scpAdpcmFadeCloseFunc(&st13c_reserved, 512);

        scpFadeOut(16.0f, 0, 0, 0);
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        jimakuUndisp(&jimaku_msg);
        scpFadeIn(3.0f);
    }

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 532);

    gsb_SetZoom(1.0f, 1000.0f);
    lt_switch_layout(PAL_JPNKORCN(54, 51));

    t = (60 - systemStatus[0] * 10) / systemStatus[1];
    _ACTWait(t * 3);

    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);

    GOBJ_SUB(boyGObj)->ctrl.blendFrames =
        (int)((float)((60 - systemStatus[0] * 10) / systemStatus[1]) / 60.0f * 30.0f);

    _ACTWait(1);

    iosOmSendMail(girlGObj, 63, boyGObj);

    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
    fightSoundProcessRequestStart();
    gflagOn(28);
}

void actSt13cHandJimaku(GObj *volatile self)
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
        case 110:
            jimaku_msg.sub.block = 11;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 340:
            jimaku_msg.sub.block = 12;
            jimaku_msg.sub.jump = -1;
            jimakuOn = 1;
            jimakuJump(&jimaku_msg);
            break;
        case 580:
            jimaku_msg.sub.block = 13;
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
    } while (t < 800.0f);
}

static inline void actSt13cSleep_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(18) == 0) {
        sleep_mes[0].func = actSt13cSleepChk;
        act->mail = sleep_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt13cSleep_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(18) == 0) {
        sleep_mes[0].func = actSt13cSleepChk;
        act->mail = sleep_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt13cSleep(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cSleep_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cSleep_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(19) == 0) {
        sleep_mes[0].func = actSt13cSleepChk;
        act->mail = sleep_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static inline void actSt13cCageDown_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(19) == 0) {
        stage_SetAnimation(59, 0, 0);
        stage_SetAnimation(63, 0, 0);

        cageDown_mes[0].func = actSt13cCageDownMain;
        act->mail = cageDown_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }

    if (gflagChk(19) != 0) {
        CameraSetCameraSet(37);
    }
}

static inline void actSt13cCageDown_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(19) == 0) {
        stage_SetAnimation(59, 0, 0);
        stage_SetAnimation(63, 0, 0);

        cageDown_mes[0].func = actSt13cCageDownMain;
        act->mail = cageDown_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }

    if (gflagChk(21) == 0 && gflagChk(19) != 0) {
        CameraSetCameraSet(37);
    }
}

void actSt13cCageDown(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cCageDown_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cCageDown_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(20) == 0) {
        if (ICO_JPNKORCN) {
            stage_SetAnimation(68, 0, 0);
        }
        stage_SetAnimation(72, 0, 0);
        if (!ICO_JPNKORCN) {
            stage_SetAnimation(76, 0, 0);
        }

        cageDown_mes[0].func = actSt13cCageDownMain;
        act->mail = cageDown_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }

    if (gflagChk(20) != 0) {
        CameraSetCameraSet(37);
    }
}

static inline void actSt13cCageFallReady_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(21) == 0) {
        cageFallReady_mes[0].func = actSt13cCageFallReadyChk;
        act->mail = cageFallReady_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt13cCageFallReady_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(21) == 0) {
        cageFallReady_mes[0].func = actSt13cCageFallReadyChk;
        act->mail = cageFallReady_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt13cCageFallReady(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cCageFallReady_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cCageFallReady_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(22) == 0) {
        cageFallReady_mes[0].func = actSt13cCageFallReadyChk;
        act->mail = cageFallReady_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static inline void actSt13cEnemy_ntsc(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    bossGenerator = self;
    Generator_Mask(self);

    while (gflagChk(22) == 0) {
        _ACTWait(1);
    }

    Generator_Call(scpSearchGobj(135));
    _ACTWait(180);

    Generator_Call(self);
    scpSleepEnemyAll();
    gflagOff(22);
}

static inline void actSt13cEnemy_aug6jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    bossGenerator = self;
    Generator_Mask(self);

    while (gflagChk(22) == 0) {
        _ACTWait(1);
    }

    Generator_Call(scpSearchGobj(134));
    _ACTWait(180);

    Generator_Call(self);
    scpSleepEnemyAll();
    gflagOff(22);
}

void actSt13cEnemy(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cEnemy_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cEnemy_aug6jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    bossGenerator = self;
    Generator_Mask(self);

    while (gflagChk(23) == 0) {
        _ACTWait(1);
    }

    Generator_Call(scpSearchGobj(152));
    _ACTWait(180);

    Generator_Call(self);
    scpSleepEnemyAll();
    gflagOff(23);
}

static inline void actSt13cEnemyNull_ntscaug6jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(24) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);
}

void actSt13cEnemyNull(GObj *volatile self)
{
    if (ICO_NTSCAUG6JUN26) {
        actSt13cEnemyNull_ntscaug6jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    Generator_Mask(self);

    while (gflagChk(25) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);
}

static inline void actSt13cSekizo_ntscaug6jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    scpSekizou(self, 29, 64, 0, 17, -300.0f, -100.0f, 100.0f, -300.0f, -100.0f, 0.0f);

    if (gflagChk(29) == 0) {
        SetWayGroupActive(2, 0);
    } else {
        SetWayGroupActive(2, 1);
    }
}

void actSt13cSekizo(GObj *volatile self)
{
    if (ICO_NTSCAUG6JUN26) {
        actSt13cSekizo_ntscaug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(31) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(77, 73), 0, 0);
        SetWayGroupActive(2, 0);

        sekizo_mes[0].func = actSt13cSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(77, 73), 0, -1);
        SetWayGroupActive(2, 1);
    }
}

static inline void actSt13cSekizoJimaku_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(29) == 0) {
        sekizoJimaku_mes[0].func = actSt13cSekizoJimakuChk;
        act->mail = sekizoJimaku_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt13cSekizoJimaku_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(29) == 0) {
        sekizoJimaku_mes[0].func = actSt13cSekizoJimakuChk;
        act->mail = sekizoJimaku_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt13cSekizoJimaku(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cSekizoJimaku_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cSekizoJimaku_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(31) == 0) {
        sekizoJimaku_mes[0].func = actSt13cSekizoJimakuChk;
        act->mail = sekizoJimaku_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static inline void actSt13cHand_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(26) == 0) {
        hand_mes[0].func = actSt13cHandChk;
        act->mail = hand_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt13cHand_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(26) == 0) {
        hand_mes[0].func = actSt13cHandChk;
        act->mail = hand_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt13cHand(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cHand_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cHand_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(27) == 0) {
        hand_mes[0].func = actSt13cHandChk;
        act->mail = hand_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static inline void actSt13cGirlCarry_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(26) == 0) {
        girlCarry_mes[0].func = actSt13cGirlCarryChk;
        act->mail = girlCarry_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt13cGirlCarry_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(26) == 0) {
        girlCarry_mes[0].func = actSt13cGirlCarryChk;
        act->mail = girlCarry_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt13cGirlCarry(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cGirlCarry_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cGirlCarry_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(27) == 0) {
        girlCarry_mes[0].func = actSt13cGirlCarryChk;
        act->mail = girlCarry_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static inline void actSt13cRescue_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(28) == 0 && gflagChk(26) == 0) {
        rescue_mes[0].func = actSt13cRescueChk;
        act->mail = rescue_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt13cRescue_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(28) == 0 && gflagChk(26) == 0) {
        rescue_mes[0].func = actSt13cRescueChk;
        act->mail = rescue_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt13cRescue(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cRescue_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cRescue_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(30) == 0 && gflagChk(27) == 0) {
        rescue_mes[0].func = actSt13cRescueChk;
        act->mail = rescue_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static inline void actSt13cBuki_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    buki_mes[0].func = actSt13cBukiChk;
    act->mail = buki_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static inline void actSt13cBuki_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    buki_mes[0].func = actSt13cBukiChk;
    act->mail = buki_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt13cBuki(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cBuki_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cBuki_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    buki_mes[0].func = actSt13cBukiChk;
    act->mail = buki_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actE3St13cSekizo(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    scpSekizou(self, 31, PAL_JPNKORCN(77, 73), 0, 17, -300.0f, -100.0f, 100.0f, -300.0f, -100.0f,
               0.0f);

    if (gflagChk(31) == 0) {
        SetWayGroupActive(2, 0);
    } else {
        SetWayGroupActive(2, 1);
    }
}

void actSt13cBmg1Event(int x)
{
    volatile int local = x;
}

void actSt13cSleepEvent(int x)
{
    volatile int local = x;
}

static inline void actSt13cSleepChk_ntsc(GObj *volatile self)
{
    long long ofs[2];
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, girlGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    scpPlayStart(girlGObj);
    _ACTWait(1);

    ofs[0] = sleepFacePos.d[0];
    ofs[1] = sleepFacePos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpPlayMotReq(girlGObj, 253);
}

static inline void actSt13cSleepChk_aug6jun26(GObj *volatile self)
{
    long long ofs[2];
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, girlGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    scpPlayStart(girlGObj);
    _ACTWait(1);

    ofs[0] = sleepFacePos.d[0];
    ofs[1] = sleepFacePos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpPlayMotReq(girlGObj, 251);
}

void actSt13cSleepChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cSleepChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cSleepChk_aug6jun26(self);
        return;
    }
    long long ofs[2];
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, girlGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    scpPlayStart(girlGObj);
    _ACTWait(1);

    ofs[0] = sleepFacePos.d[0];
    ofs[1] = sleepFacePos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpPlayMotReq(girlGObj, 285);
}

static inline void actSt13cCageDownMain_aug6jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 0;
    sub->mainMail = cageDownMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt13cCageDownMain(GObj *volatile self)
{
    if (ICO_AUG6JUN26) {
        actSt13cCageDownMain_aug6jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = cageDownMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

static inline void actSt13cCageDownSwitch_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    scpBoyControlReadDisable = 1;

    if (gflagChk(19) == 0) {
        cageDownSwitch_mes[0].func = actSt13cCage1stDown;
        sub->mail = cageDownSwitch_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static inline void actSt13cCageDownSwitch_aug6jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    scpBoyControlReadDisable = 1;

    if (gflagChk(19) == 0) {
        cageDownSwitch_mes[0].func = actSt13cCage1stDown;
        sub->mail = cageDownSwitch_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt13cCageDownSwitch(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cCageDownSwitch_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cCageDownSwitch_aug6jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    sub->mainMail = 0;
    scpBoyControlReadDisable = 1;

    if (gflagChk(20) == 0) {
        cageDownSwitch_mes[0].func = actSt13cCage1stDown;
        sub->mail = cageDownSwitch_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static inline void actSt13cCage1stDownDemo_ntscaug6jun26(GObj *volatile self)
{
    scpPlayStart(boyGObj);
    scpPlayMot(boyGObj, 307);
    scpPlayWaitMotEnd(boyGObj);
    _ACTWait(240);
    demoEnd = 1;
    _ACTWait(0);
}

void actSt13cCage1stDownDemo(GObj *volatile self)
{
    if (ICO_NTSCAUG6JUN26) {
        actSt13cCage1stDownDemo_ntscaug6jun26(self);
        return;
    }
    scpPlayStart(boyGObj);
    scpPlayMot(boyGObj, 313);
    scpPlayWaitMotEnd(boyGObj);
    _ACTWait(240);
    demoEnd = 1;
    _ACTWait(0);
}

static inline void actSt13cCageFallReadyChk_ntscaug6jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    while (scpTriggerFloorAttr(boyGObj, 0x2000000) == 0 || gflagChk(19) == 0) {
        _ACTWait(1);
    }

    scpAdpcmPlayRequestFunc(15, &st13c_adpcm2, 1, 1, 0);
}

void actSt13cCageFallReadyChk(GObj *volatile self)
{
    if (ICO_NTSCAUG6JUN26) {
        actSt13cCageFallReadyChk_ntscaug6jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    while (scpTriggerFloorAttr(boyGObj, 0x2000000) == 0 || gflagChk(20) == 0) {
        _ACTWait(1);
    }

    scpAdpcmPlayRequestFunc(15, &st13c_adpcm2, 1, 1, 0);
}

void actSt13cCageFallEvent(int x)
{
    volatile int local = x;
}

static inline void actE3St13cSekizoEvent_aug6jun26(
    int x) /* the prototype's TRFILE.TXT spells it actSt13cSekizoEvent */
{
    volatile int local = x;
}

static void actE3St13cSekizoEvent(int x)
{
    if (ICO_AUG6JUN26) {
        actE3St13cSekizoEvent_aug6jun26(x);
        return;
    }
    volatile int local = x;
}

static inline void actSt13cSekizoJimakuChk_ntsc(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    while (gflagChk(29) == 0) {
        _ACTWait(1);
    }

    gflagOff(357);
    actCreateSubThread(actSt13cSekizoJimakuEff, 21);
}

static inline void actSt13cSekizoJimakuChk_aug6jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    while (gflagChk(29) == 0) {
        _ACTWait(1);
    }

    actCreateSubThread(actSt13cSekizoJimakuEff, 21);
}

void actSt13cSekizoJimakuChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cSekizoJimakuChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cSekizoJimakuChk_aug6jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);

    _ACTWait(1);

    while (gflagChk(31) == 0) {
        _ACTWait(1);
    }

    gflagOff(390);
    actCreateSubThread(actSt13cSekizoJimakuEff, 21);
}

void actSt13cSekizoJimakuEff(GObj *volatile self)
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
        case 45:
            jimaku_msg.sub.block = 19;
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
    } while (t < 500.0f);
}

static inline void actSt13cGirlCarryAgainChk_ntsc(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (GOBJ_ACT(girlGObj)->actMode != 107) {
        _ACTWait(1);
    }

    gflagOff(25);

    girlCarryAgainChk_mes[0].func = actSt13cGirlCarryChk;
    act->mail = girlCarryAgainChk_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);

    _ACTWait(1);
}

static inline void actSt13cGirlCarryAgainChk_aug6jun26(GObj *volatile self)
{
    Act *act = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (GOBJ_ACT(girlGObj)->actMode != 107) {
        _ACTWait(1);
    }

    gflagOff(25);

    girlCarryAgainChk_mes[0].func = actSt13cGirlCarryChk;
    act->mail = girlCarryAgainChk_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);

    _ACTWait(1);
}

void actSt13cGirlCarryAgainChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cGirlCarryAgainChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cGirlCarryAgainChk_aug6jun26(self);
        return;
    }
    Act *act = GOBJ_ACT(self);

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (GOBJ_ACT(girlGObj)->actMode != 111) {
        _ACTWait(1);
    }

    gflagOff(26);

    girlCarryAgainChk_mes[0].func = actSt13cGirlCarryChk;
    act->mail = girlCarryAgainChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);

    _ACTWait(1);
}

static void actSt13cHandSub(GObj *volatile self)
{
    _ACTWait(100);
    scpPlayWaitMotEnd(boyGObj);
    demoEnd = 1;
    _ACTWait(0);
}

static inline void actSt13cRescueChk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (GOBJ_ACT(girlGObj)->actMode != 106) {
        _ACTWait(1);
    }

    gflagOn(27);
    scpBoyControlReadDisable = 1;
    _ACTWait(200);

    scpPlayStart(girlGObj);
    scpPlayMot(girlGObj, 552);
    _ACTWait(120);

    gflagOn(28);
}

static inline void actSt13cRescueChk_aug6jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (GOBJ_ACT(girlGObj)->actMode != 106) {
        _ACTWait(1);
    }

    gflagOn(27);
    scpBoyControlReadDisable = 1;
    _ACTWait(200);

    scpPlayStart(girlGObj);
    scpPlayMot(girlGObj, 553);
    _ACTWait(120);

    gflagOn(28);
}

void actSt13cRescueChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt13cRescueChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt13cRescueChk_aug6jun26(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (GOBJ_ACT(girlGObj)->actMode != 110) {
        _ACTWait(1);
    }

    gflagOn(29);
    scpBoyControlReadDisable = 1;
    _ACTWait(200);

    scpPlayStart(girlGObj);
    scpPlayMot(girlGObj, 596);
    _ACTWait(120);

    gflagOn(30);
}

void actSt13cBukiEvent(int x)
{
    volatile int local = x;
}

static inline void actSt13cBukiChk_ntscaug6jun26(GObj *volatile self)
{
    while (ForMotionViewer_GetCurrentMotion(boyGObj) != 227) {
        _ACTWait(1);
    }
    stage_SetAnimation(63, -1, -2);
}

void actSt13cBukiChk(GObj *volatile self)
{
    if (ICO_NTSCAUG6JUN26) {
        actSt13cBukiChk_ntscaug6jun26(self);
        return;
    }
    while (ForMotionViewer_GetCurrentMotion(boyGObj) != 231) {
        _ACTWait(1);
    }
    stage_SetAnimation(PAL_JPNKORCN(76, 72), -1, -2);
}
