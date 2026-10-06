#include "end.h"
#include "mcard.h"
#include "layout_texture.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "act.h"
#include "boyact.h"
#include "commonact.h"
#include "jimaku.h"
#include "camera-root.h"
#include "gflag.h"
#include "RegistPacket.h"
#include "StageAnimation.h"
#include "boy.h"
#include "girl.h"
#include "item.h"
#include "staticBlur.h"
#include <libvu0.h>
#include "e3.h"
#include "typedef.h"
#include "layout_action.h"
#include "GsBase.h"
#include "main.h"
#include "script.h"
#include "staffroll.h"
#include "ico_gamestate.h" /* port: achievement signals, docs/port/ACHIEVEMENTS.md */

static void actEndingSave(GObj *volatile self);

/* .sdata: the ending scenes' stream handles (ed5 and happy_end unused), then
   the staff roll's, the fourteenth demo's and the st27a ending's. */
SqEntry *ed1 = 0;

SqEntry *ed2 = 0;

SqEntry *ed3 = 0;

SqEntry *ed4 = 0;

int ed5 = 0;

SqEntry *ed6 = 0;

SqEntry *sea = 0;

int happy_end = 0;

static SqEntry *staff3 = 0; /* derived name */

static SqEntry *endDemo14 = 0; /* derived name */

static SqEntry *st27aEnd = 0; /* derived name */

static ActMail demo01_mes[2] = {{430}, {429}}; /* derived name */

static ActMail demo02_mes[2] = {{430}, {429}}; /* derived name */

static ActMail demo03_mes[2] = {{430}, {429}}; /* derived name */

static ActMail demo04_mes[2] = {{430}, {429}}; /* derived name */

static ActMail demo05_mes[2] = {{430}, {429}}; /* derived name */

static ActMail demo06_mes[2] = {{430}, {429}}; /* derived name */

static ActMail demo07_mes[2] = {{430}, {429}}; /* derived name */

static ActMail demo10_mes[2] = {{430}, {429}}; /* derived name */

static ActMail demo11_mes[2] = {{430}, {429}}; /* derived name */

static ActMail demo12_mes[2] = {{430}, {429}}; /* derived name */

static ActMail demo13_mes[2] = {{430}, {429}}; /* derived name */

static ActMail staff1_mes[2] = {{430}, {429}}; /* derived name */

static ActMail staff2_mes[2] = {{430}, {429}}; /* derived name */

static ActMail staff3_mes[2] = {{430}, {429}}; /* derived name */

static ActMail ed_demo14_mes[2] = {{430}, {429}}; /* derived name */

static ActMail st27aEnd_mes[2] = {{430}, {429}}; /* derived name */

static ActMail logo_mes[2] = {{430}, {429}}; /* derived name */

static ActMail end_mes[2] = {{430}, {429}}; /* derived name */

void actEndDemo01(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(339) == 0 && gflagChk(343) == 0) {
        lt_switch_layout(55);
        scpBoyControlReadDisable = 1;
        scpFadeOut(255.0f, 0, 0, 0);
        stage_SetAnimation(840, 0, 0);
        stage_SetAnimation(32, 0, -1);
        stage_SetAnimation(26, 0, 0);
        stage_SetAnimation(35, 0, -1);
        stage_SetAnimation(33, 0, -1);
        stage_SetAnimation(34, 0, -1);
        stage_SetAnimation(36, 0, 0);
        stage_SetAnimation(38, 0, 0);
        demo01_mes[0].func = actEndDemo01Chk;
        act->mail = demo01_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actConte14_1(GObj *volatile self)
{
    scpPlayStart(boyGObj);

    stage_SetAnimation(814, 1, 0);

    scpPlayMot(scpSearchGobj(2515), 809);

    scpSearchGobj(2515)->active = 1;

    while (stage_ContinueAnimation(814, 815) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(815, 816) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(55, 1, 0);

    while (stage_ContinueAnimation(816, 817) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(817, 818) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(818, 819) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(2515), 810);

    while (stage_ContinueAnimation(819, 820) == 0) {
        _ACTWait(1);
    }

    scpSearchGobj(2515)->active = 0;

    scpPlayMot(scpSearchGobj(2516), 811);

    scpSearchGobj(2516)->active = 1;

    scpLinkBGAtoLayoutedTargetSkeltonWithLocalRotationFlag(2516, 0, 554, 0);

    stage_SetLoopFlag(554, 1);

    stage_SetAnimation(554, 1, 0);

    while (stage_ContinueAnimation(820, 821) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(2516), 812);

    while (stage_ContinueAnimation(821, 822) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(2516), 813);

    while (stage_ContinueAnimation(822, 823) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(2516), 814);

    while (stage_CheckAnimationFinish(823) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    RequestStageChange(2, boyGObj, 0, 1.0f, 8.0f);
}

void actEndDemo02(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (boyGObj != 0) {
        scpPlayMot(boyGObj, 0);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpFadeOut(255.0f, 0, 0, 0);
    scpLinkBGAtoKindTargetSkeltonWithLocalRotationFlag(3, 0, 554, 0);
    stage_SetLoopFlag(554, 1);
    stage_SetAnimation(554, 1, 0);
    stage_SetAnimation(825, 0, 0);
    stage_SetAnimation(556, 0, -1);
    stage_SetAnimation(557, 0, -1);
    stage_SetAnimation(559, 0, -1);
    stage_SetAnimation(560, 0, -1);
    stage_SetAnimation(561, 0, -1);
    SelectBoyCrown(boyGObj, 2);
    demo02_mes[0].func = actEndDemo02Chk;
    act->mail = demo02_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actConte14_2(GObj *volatile self)
{
    scpPlayStart(boyGObj);

    stage_SetAnimation(824, 1, 0);

    scpPlayMot(boyGObj, 441);

    while (stage_ContinueAnimation(824, 825) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(825, 826) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(2686), 815);

    scpSearchGobj(2686)->active = 1;

    _ACTWait(1);

    stage_SetAnimation(827, 1, 0);

    while (stage_ContinueAnimation(826, 828) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 442);

    scpPlayMot(scpSearchGobj(2686), 816);

    while (stage_ContinueAnimation(828, 829) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 443);

    scpPlayMot(scpSearchGobj(2686), 817);

    while (stage_ContinueAnimation(829, 830) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 444);

    scpPlayMot(scpSearchGobj(2686), 818);

    while (stage_ContinueAnimation(830, 831) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 445);

    scpPlayMot(scpSearchGobj(2686), 819);

    _ACTWait(1);

    stage_SetAnimation(832, 1, 0);

    while (stage_CheckAnimationFinish(831) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayEnd(boyGObj);

    lt_switch_layout(54);

    RequestStageChange(3, boyGObj, 0, 1.0f, 8.0f);
}

void actEndDemo06(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(344) == 0 && gflagChk(343) != 0) {
        lt_switch_layout(55);
        scpBoyControlReadDisable = 1;
        scpFadeOut(255.0f, 0, 0, 0);
        scpLinkBGAtoLayoutedTargetSkeltonWithLocalRotationFlag(2516, 0, 554, 0);
        stage_SetLoopFlag(554, 1);
        stage_SetAnimation(554, 1, 0);
        stage_SetAnimation(840, 0, 0);
        stage_SetAnimation(32, 0, -1);
        stage_SetAnimation(26, 0, 0);
        stage_SetAnimation(35, 0, -1);
        stage_SetAnimation(33, 0, -1);
        stage_SetAnimation(34, 0, -1);
        stage_SetAnimation(36, 0, 0);
        stage_SetAnimation(38, 0, 0);
        SelectBoyCrown(boyGObj, 2);
        demo06_mes[0].func = actEndDemo06Chk;
        act->mail = demo06_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actConte14_6(GObj *volatile self)
{
    preload(3);

    scpPlayStart(boyGObj);

    stage_SetAnimation(838, 1, 0);

    stage_SetAnimation(840, 1, 0);

    stage_SetAnimation(841, 1, 0);

    scpPlayMot(boyGObj, 446);

    scpPlayMot(scpSearchGobj(2516), 820);

    scpSearchGobj(2516)->active = 1;

    while (stage_ContinueAnimation(838, 839) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 447);

    scpPlayMot(scpSearchGobj(2516), 821);

    while (stage_CheckAnimationFinish(839) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    RequestStageChange(3, boyGObj, girlGObj, 1.0f, 8.0f);
}

void actEndDemo07(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpFadeOut(255.0f, 0, 0, 0);
    scpLinkBGAtoKindTargetSkeltonWithLocalRotationFlag(3, 0, 554, 0);
    stage_SetAnimation(173, 0, 0);
    stage_SetLoopFlag(554, 1);
    stage_SetAnimation(554, 1, 0);

    if (gflagChk(345) == 0) {
        SelectBoyCrown(boyGObj, 2);
        demo07_mes[0].func = actEndDemo07Chk;
        act->mail = demo07_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

/* 16-byte constant vector templates: the float view carries the values, the
   long long view is the one the copy reads. */

static const ConstVec conte14_7Pos = {{-4743.0f, -661.0f, 2503.0f, 1.0f}}; /* derived name */

static const ConstVec staff3DemoPos = {{-800.0f, 0.0f, -1000.0f, 1.0f}}; /* derived name */

static const ConstVec conte14_14Pos = {{16975.0f, 71.0f, -4332.0f, 1.0f}}; /* derived name */

void actConte14_7(GObj *volatile self)
{
    long long ofs[2];
    float dir[4];

    preload(6);

    scpPlayStart(boyGObj);

    stage_SetAnimation(842, 1, 0);

    stage_SetAnimation(173, 1, 471);

    _ACTWait(1);

    scpPlayPosSet(boyGObj, -4757.0f, -660.0f, 2646.0f);

    scpPlayPosSet(scpSearchGobj(2388), -4757.0f, -664.0f, 2646.0f);

    _ACTWait(1);

    ofs[0] = conte14_7Pos.d[0];
    ofs[1] = conte14_7Pos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    ofs[0] = conte14_7Pos.d[0];
    ofs[1] = conte14_7Pos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(scpSearchGobj(2388)));
    scpPlayMotDir(scpSearchGobj(2388), dir);

    scpPlayMotNode(boyGObj, 448, scpSearchGobj(2388), 44);

    scpPlayMot(scpSearchGobj(2388), 822);

    scpSearchGobj(2388)->active = 1;

    while (stage_CheckAnimationFinish(842) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    RequestStageChange(6, boyGObj, girlGObj, 1.0f, 8.0f);
}

void actEndDemo10(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpFadeOut(255.0f, 0, 0, 0);
    scpLinkBGAtoKindTargetSkeltonWithLocalRotationFlag(3, 0, 554, 0);
    scpLinkBGAtoKindTargetSkeltonWithLocalRotationFlag(3, 52, 58, 0);
    stage_SetLoopFlag(554, 1);
    stage_SetAnimation(554, 1, 0);
    SelectBoyCrown(boyGObj, 2);
    stage_SetAnimation(850, 0, 0);
    demo10_mes[0].func = actEndDemo10Chk;
    act->mail = demo10_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actConte14_10(GObj *volatile self)
{
    preload(3);

    scpPlayStart(boyGObj);

    stage_SetAnimation(843, 1, 0);

    scpPlayMot(boyGObj, 449);

    scpPlayMot(scpSearchGobj(2122), 823);

    scpSearchGobj(2122)->active = 1;

    _ACTWait(1);

    stage_SetAnimation(45, 1, 0);

    stage_SetAnimation(844, 1, 0);

    while (stage_ContinueAnimation(843, 845) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 450);

    scpPlayMot(scpSearchGobj(2122), 824);

    _ACTWait(1);

    stage_SetAnimation(46, 1, 0);

    while (stage_ContinueAnimation(845, 846) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 451);

    scpPlayMot(scpSearchGobj(2122), 825);

    _ACTWait(1);

    stage_SetAnimation(47, 1, 0);

    while (stage_ContinueAnimation(846, 847) == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(ed6->stream);

    scpPlayMot(boyGObj, 452);

    scpPlayMot(scpSearchGobj(2122), 826);

    _ACTWait(1);

    stage_SetAnimation(48, 1, 0);

    while (stage_ContinueAnimation(847, 848) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 453);

    scpPlayMot(scpSearchGobj(2122), 827);

    _ACTWait(1);

    stage_SetAnimation(49, 1, 0);

    while (stage_ContinueAnimation(848, 849) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 454);

    _ACTWait(1);

    stage_SetAnimation(50, 1, 0);

    stage_SetAnimation(56, 1, 0);

    stage_SetAnimation(850, 1, 0);

    while (stage_ContinueAnimation(849, 851) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 455);

    _ACTWait(1);

    stage_SetAnimation(51, 1, 0);

    stage_SetAnimation(57, 1, 0);

    while (stage_CheckAnimationFinish(851) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    RequestStageChange(3, boyGObj, girlGObj, 1.0f, 8.0f);
}

void actConte14_13(GObj *volatile self)
{
    scpPlayStart(boyGObj);

    scpFadeIn(3.0f);

    preload(6);

    stage_SetAnimation(854, 1, 0);

    scpPlayMot(boyGObj, 456);

    while (stage_ContinueAnimation(854, 855) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(53, 1, 0);

    stage_SetAnimation(465, 1, 0);

    scpPlayMot(boyGObj, 456);

    while (stage_CheckAnimationFinish(855) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayEnd(boyGObj);

    RequestStageChange(6, boyGObj, girlGObj, 1.0f, 8.0f);
}

void actStaff1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    stage_SetAnimation(30, 0, -1);
    stage_SetAnimation(26, 0, 0);
    stage_SetAnimation(33, 0, 0);
    stage_SetAnimation(34, 0, 0);
    stage_SetAnimation(36, 0, 0);
    stage_SetAnimation(38, 0, 0);
    stage_SetAnimation(35, 0, 0);
    staff1_mes[0].func = actStaff1Chk;
    act->mail = staff1_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actStaff1Demo(GObj *volatile self)
{
    preload(1);

    staffRollStart(1.0f, 255);

    stage_SetAnimation(870, 1, 0);

    while (stage_ContinueAnimation(870, 871) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(26, 1, 0);

    scpPlayMot(boyGObj, 288);

    while (stage_ContinueAnimation(871, 873) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(873, 874) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 289);

    _ACTWait(130);

    stage_SetAnimation(32, 1, 0);

    while (stage_ContinueAnimation(874, 875) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(875, 876) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 290);

    while (stage_ContinueAnimation(876, 877) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(877, 878) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 292);

    _ACTWait(1);

    stage_SetAnimation(32, 1, 208);

    while (stage_ContinueAnimation(878, 879) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 293);

    while (stage_ContinueAnimation(879, 880) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 294);

    while (stage_ContinueAnimation(880, 881) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 295);

    while (stage_ContinueAnimation(881, 882) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 296);

    while (stage_ContinueAnimation(882, 883) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 297);

    while (stage_CheckAnimationFrame(883, 100, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpFadeOut(8.0f, 0, 0, 0);

    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    RequestStageChange(1, boyGObj, girlGObj, 0.0f, 8.0f);
}

void actStaff2Demo(GObj *volatile self)
{
    preload(1);

    stage_SetAnimation(907, 1, 0);

    stage_SetAnimation(269, 1, 0);

    scpPlayMot(boyGObj, 327);

    scpPlayMot(scpSearchGobj(2789), 737);

    scpSearchGobj(2789)->active = 1;

    while (stage_ContinueAnimation(907, 908) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 328);

    scpPlayMot(scpSearchGobj(2789), 738);

    while (stage_ContinueAnimation(908, 909) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 329);

    scpPlayMot(scpSearchGobj(2789), 739);

    while (stage_ContinueAnimation(909, 910) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(910, 911) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 330);

    scpPlayMot(scpSearchGobj(2789), 740);

    while (stage_ContinueAnimation(911, 912) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 331);

    scpPlayMot(scpSearchGobj(2789), 741);

    while (stage_ContinueAnimation(912, 913) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 332);

    scpPlayMot(scpSearchGobj(2789), 742);

    while (stage_ContinueAnimation(913, 914) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 333);

    while (stage_ContinueAnimation(914, 929) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 341);

    scpPlayMot(scpSearchGobj(2789), 748);

    while (stage_ContinueAnimation(929, 930) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 342);

    scpPlayMot(scpSearchGobj(2789), 749);

    while (stage_ContinueAnimation(930, 931) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 343);

    scpPlayMot(scpSearchGobj(2789), 750);

    while (stage_ContinueAnimation(931, 932) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 344);

    scpPlayMot(scpSearchGobj(2789), 751);

    while (stage_CheckAnimationFrame(932, 200, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    reg_SetScissorSw(0);

    SetStaticBlur(1);

    scpFadeOut(8.0f, 0, 0, 0);

    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    RequestStageChange(1, boyGObj, girlGObj, 0.0f, 8.0f);
}

void actStaff3Demo(GObj *volatile self)
{
    long long ofs[2];
    float dir[4];

    stage_SetAnimation(884, 1, 0);

    scpPlayPosSet(scpSearchGobj(2841), -7.0f, -5725.0f, 18.0f);

    ofs[0] = staff3DemoPos.d[0];
    ofs[1] = staff3DemoPos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(scpSearchGobj(2841)));
    scpPlayMotDir(scpSearchGobj(2841), dir);

    scpPlayMot(scpSearchGobj(2841), 551);

    scpSearchGobj(2841)->active = 1;

    scpPlayMot(boyGObj, 308);

    while (stage_ContinueAnimation(884, 885) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 309);

    while (stage_ContinueAnimation(885, 886) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 310);

    while (stage_ContinueAnimation(886, 887) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 311);

    while (stage_ContinueAnimation(887, 895) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(74, 0, -1);

    scpPlayMot(boyGObj, 318);

    scpPlayMot(scpSearchGobj(2841), 728);

    while (stage_ContinueAnimation(895, 896) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 318);

    scpPlayMot(scpSearchGobj(2841), 729);

    while (stage_ContinueAnimation(896, 897) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 318);

    scpPlayMot(scpSearchGobj(2841), 730);

    while (stage_ContinueAnimation(897, 898) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 319);

    scpPlayMot(scpSearchGobj(2841), 731);

    while (stage_ContinueAnimation(898, 900) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 320);

    scpPlayMot(scpSearchGobj(2841), 732);

    while (stage_CheckAnimationFrame(900, 200, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpFadeOut(6.0f, 0, 0, 0);
}

void actEndDemo14(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    fbKeep = 1;
    scpFadeOut(3.0f, 255, 255, 255);

    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    fbKeep = 0;
    ScpCallCameraSetTarget(7046.0f, -77.0f, 1678.0f);

    scpAdpcmPlayRequestFunc(95, &endDemo14, 1, 0, 1);

    while (endDemo14 == 0) {
        _ACTWait(1);
    }

    SelectBoyCrown(boyGObj, 2);
    SetGirlClothDispSwitch(scpSearchGobj(2253), 1, 2);
    ed_demo14_mes[0].func = actEndDemo14Chk;
    act->mail = ed_demo14_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actConte14_14(GObj *volatile self)
{
    long long ofs[2];
    float dir[4];

    scpPlayStart(boyGObj);

    scpFadeIn(6.0f);

    stage_SetAnimation(860, 1, 0);

    scpPlayMot(boyGObj, 461);

    while (stage_ContinueAnimation(860, 861) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 462);

    while (stage_ContinueAnimation(861, 862) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 463);

    _ACTWait(1);

    stage_SetAnimation(54, 1, 0);

    while (stage_ContinueAnimation(862, 863) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 464);

    while (stage_ContinueAnimation(863, 864) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 465);

    while (stage_ContinueAnimation(864, 865) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 466);

    scpPlayMot(scpSearchGobj(2253), 829);

    scpSearchGobj(2253)->active = 1;

    while (stage_CheckAnimationFinish(865) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    _ACTWait(180);

    scpPlayMot(boyGObj, 0);

    ofs[0] = conte14_14Pos.d[0];
    ofs[1] = conte14_14Pos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpPlayEnd(boyGObj);

    scpBoyControlReadDisable = 0;

    lt_switch_layout(54);

    gflagOff(338);
}

void actSt27aEnd(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gFlagGameClear == 0) {
        scpSearchGobj(2256)->active = 0;
        scpSearchGobj(2257)->active = 0;
        scpSearchGobj(2258)->active = 0;
        scpSearchGobj(2259)->active = 0;
        scpSearchGobj(2260)->active = 0;
        scpSearchGobj(2261)->active = 0;
        scpSearchGobj(2262)->active = 0;
        scpSearchGobj(2263)->active = 0;
    }

    if (gflagChk(355) == 0) {
        st27aEnd_mes[0].func = actSt27aEndChk;
        act->mail = st27aEnd_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt27aEndChk(GObj *volatile self)
{
    float max;
    float t;

    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;

    if (GetCharHeldItem(boyGObj) == 3) {
        gflagOn(354);
        scpPlayMot(boyGObj, 157);
    }

    if (endDemo14 != 0) {
        scpAdpcmFadeCloseFunc(&endDemo14, 27);
    }
    scpAdpcmPlayRequestFunc(51, &st27aEnd, 0, 1, 1);
    while (st27aEnd == 0) {
        _ACTWait(1);
    }

    gflagOn(355);

    actCreateSubThread(actSt27aEndDemo, 21);

    max = (float)((60 - systemStatus[0] * 10) / systemStatus[1] * 20);
    t = (float)((60 - systemStatus[0] * 10) / systemStatus[1] * 20);
    while (t > 0.0f) {
        scpSeEnvMasterVolRate = t / max;
        t -= 1.0f;
        _ACTWait(1);
    }
    scpSeEnvMasterVolRate = 0;
}

void actSt27aEndDemo(GObj *volatile self)
{
    stage_SetAnimation(866, 1, 0);

    if (gflagChk(354) == 0) {
        scpPlayMot(boyGObj, 467);
    } else {
        scpPlayMot(boyGObj, 470);
    }

    scpPlayMot(scpSearchGobj(2253), 830);

    scpSearchGobj(2253)->active = 1;

    while (stage_ContinueAnimation(866, 867) == 0) {
        _ACTWait(1);
    }

    if (gflagChk(354) == 0) {
        scpPlayMot(boyGObj, 468);
    } else {
        scpPlayMot(boyGObj, 471);
    }

    scpPlayMot(scpSearchGobj(2253), 831);

    while (stage_ContinueAnimation(867, 868) == 0) {
        _ACTWait(1);
    }

    SetHandCameraLimitInDemo(10, 10);

    SetZoomMaxValInDemo(30);

    if (gflagChk(354) == 0) {
        scpPlayMot(boyGObj, 469);
    } else {
        scpPlayMot(boyGObj, 472);
    }

    scpPlayMot(scpSearchGobj(2253), 832);

    if (gflagChk(354) == 0) {
        while (stage_CheckAnimationFinish(868) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        ResetHandCameraLimitInDemo();

        ResetZoomMaxValInDemo();

        RequestStageChange(1, boyGObj, girlGObj, 1.0f, 8.0f);
    } else {
        if (st27aEnd != 0) {
            scpAdpcmFadeCloseFunc(&st27aEnd, 80);
        }

        while (stage_CheckAnimationFrame(868, 390, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        ResetHandCameraLimitInDemo();

        ResetZoomMaxValInDemo();

        scpFadeOut(6.0f, 0, 0, 0);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }

        scpDisActivateAllWithKind(19);

        scpFadeIn(1.5f);

        stage_SetAnimation(869, 1, 0);

        stage_SetAnimation(198, 1, 0);

        scpPlayMot(boyGObj, 473);

        scpPlayMot(scpSearchGobj(2253), 833);

        while (stage_CheckAnimationFrame(869, 390, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        scpFadeOut(3.0f, 0, 0, 0);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }

        _ACTWait(10);

        scpPlayMot(boyGObj, 0);

        _ACTWait(10);

        RequestStageChange(1, boyGObj, girlGObj, 1.0f, 8.0f);
    }
}

void actEndLogoChk(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    int id = 60;

    _ACTWait(1);

    scpBoyControlReadDisable = 1;
    enable_game_pause = 0;

    switch (NonLinearCameraMove) {
    case 3:
        id = 61;
        break;
    case 4:
        id = 62;
        break;
    case 6:
        id = 64;
        break;
    case 5:
        id = 63;
        break;
    }

    stage_SetLoopFlag(id, 1);

    stage_SetAnimation(id, 1, 0);

    SetHandCameraLimitInDemo(0, 0);

    SetZoomMaxValInDemo(0);

    preload(1);

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 30);

    scpFadeOut(6.0f, 0, 0, 0);

    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(id, -1, -2);

    scpFadeIn(6.0f);

    end_mes[0].func = actEndingSave;
    act->mail = end_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actEndDemo03(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (boyGObj != 0) {
        scpPlayMot(boyGObj, 0);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpFadeOut(255.0f, 0, 0, 0);
    stage_SetAnimation(74, 0, -1);
    stage_SetAnimation(75, 0, -1);
    stage_SetAnimation(77, 0, -1);
    demo03_mes[0].func = actEndDemo03Chk;
    act->mail = demo03_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actEndDemo04(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (boyGObj != 0) {
        scpPlayMot(boyGObj, 0);
    }

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpFadeOut(255.0f, 0, 0, 0);
    demo04_mes[0].func = actEndDemo04Chk;
    act->mail = demo04_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actEndDemo05(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    demo05_mes[0].func = actEndDemo05Chk;
    act->mail = demo05_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actEndDemo11(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    SelectBoyCrown(boyGObj, 2);
    demo11_mes[0].func = actEndDemo11Chk;
    act->mail = demo11_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actEndDemo12(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    stage_SetAnimation(80, 0, -1);
    demo12_mes[0].func = actEndDemo12Chk;
    act->mail = demo12_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actEndDemo13(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpFadeOut(255.0f, 255, 255, 255);
    SelectBoyCrown(boyGObj, 2);
    demo13_mes[0].func = actEndDemo13Chk;
    act->mail = demo13_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actStaff2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpSetBoyWeaponGObj(scpSearchGobj(2795));
    scpSearchGobj(2793)->dobj->ctrl.seMute = 1;
    scpSearchGobj(2794)->dobj->ctrl.seMute = 1;
    stage_SetAnimation(269, 0, 0);
    staff2_mes[0].func = actStaff2Chk;
    act->mail = staff2_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actStaff3(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpFadeOut(255.0f, 0, 0, 0);
    DeleteBoyWeapon();
    stage_SetAnimation(72, 0, 0);
    stage_SetAnimation(77, 0, 0);
    staff3_mes[0].func = actStaff3Chk;
    act->mail = staff3_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actEndLogo(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    logo_mes[0].func = actEndLogoChk;
    act->mail = logo_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actEndDemo01Chk(GObj *volatile self)
{
    scpAdpcmPlayRequestFunc(43, &ed1, 0, 1, 1);

    while (ed1 == 0) {
        _ACTWait(1);
    }

    preload(2);

    scpFadeIn(6.0f);

    gflagOn(2);

    gflagOn(3);

    gflagOn(4);

    gflagOn(339);

    actCreateSubThread(actConte14_1, 21);
}

void actEndDemo02Chk(GObj *volatile self)
{
    scpAdpcmPlayRequestFunc(44, &ed2, 0, 1, 1);

    while (ed2 == 0) {
        _ACTWait(1);
    }

    preload(3);

    scpFadeIn(6.0f);

    gflagOn(340);

    actCreateSubThread(actConte14_2, 21);
}

void actEndDemo03Chk(GObj *volatile self)
{
    scpAdpcmPlayRequestFunc(45, &ed3, 0, 1, 1);

    while (ed3 == 0) {
        _ACTWait(1);
    }

    preload(2);

    scpFadeIn(6.0f);

    gflagOn(341);

    actCreateSubThread(actConte14_3, 21);
}

void actConte14_3(GObj *volatile self)
{
    stage_SetAnimation(833, 1, 0);

    while (stage_CheckAnimationFinish(833) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    RequestStageChange(2, boyGObj, 0, 1.0f, 8.0f);
}

void actEndDemo04Chk(GObj *volatile self)
{
    scpAdpcmPlayRequestFunc(46, &ed4, 0, 1, 1);

    while (ed4 == 0) {
        _ACTWait(1);
    }

    preload(5);

    scpFadeIn(6.0f);

    gflagOn(342);

    actCreateSubThread(actConte14_4, 21);
}

void actConte14_4(GObj *volatile self)
{
    stage_SetAnimation(834, 1, 0);

    while (stage_CheckAnimationFinish(834) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    RequestStageChange(5, boyGObj, girlGObj, 1.0f, 8.0f);
}

void actEndDemo05Chk(GObj *volatile self)
{
    gflagOn(343);

    actCreateSubThread(actConte14_5, 21);
}

void actConte14_5(GObj *volatile self)
{
    preload(6);

    stage_SetAnimation(835, 1, 0);

    while (stage_CheckAnimationFinish(835) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    RequestStageChange(6, boyGObj, girlGObj, 1.0f, 8.0f);
}

void actEndDemo06Chk(GObj *volatile self)
{
    scpAdpcmPlayRequestFunc(47, &sea, 0, 1, 1);

    while (sea == 0) {
        _ACTWait(1);
    }

    scpFadeIn(6.0f);

    gflagOff(339);

    gflagOff(343);

    actCreateSubThread(actConte14_6, 21);
}

void actEndDemo07Chk(GObj *volatile self)
{
    _ACTWait(30);

    scpFadeIn(6.0f);

    gflagOn(345);

    actCreateSubThread(actConte14_7, 21);
}

void actEndDemo10Chk(GObj *volatile self)
{
    scpAdpcmPlayRequestFunc(48, &ed6, 0, 1, 0);

    while (ed6 == 0) {
        _ACTWait(1);
    }

    scpFadeIn(6.0f);

    gflagOn(348);

    actCreateSubThread(actConte14_10, 21);

    actCreateSubThread(actConte14_10_Jimaku, 21);
}

void actConte14_10_Jimaku(GObj *volatile self)
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
        case 1500:
            jimaku_msg.sub.block = 112;
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
    } while (t < 1800.0f);
}

void actEndDemo11Chk(GObj *volatile self)
{
    gflagOn(349);

    actCreateSubThread(actConte14_11, 21);
}

void actConte14_11(GObj *volatile self)
{
    preload(2);

    scpPlayStart(boyGObj);

    stage_SetAnimation(852, 1, 0);

    while (stage_CheckAnimationFinish(852) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    RequestStageChange(2, boyGObj, girlGObj, 1.0f, 8.0f);
}

void actEndDemo12Chk(GObj *volatile self)
{
    gflagOn(350);

    actCreateSubThread(actConte14_12, 21);
}

void actConte14_12(GObj *volatile self)
{
    stage_SetAnimation(853, 1, 0);

    preload(4);

    while (stage_CheckAnimationFinish(853) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    RequestStageChangeWithColor(4, boyGObj, girlGObj, 16.0f, 16.0f, 255, 255, 255);
}

void actEndDemo13Chk(GObj *volatile self)
{
    gflagOn(351);

    actCreateSubThread(actConte14_13, 21);
}

void actStaff1Chk(GObj *volatile self)
{
    actCreateSubThread(actStaff1Demo, 21);
}

void actStaff2Chk(GObj *volatile self)
{
    reg_SetScissorSw(1);

    SetStaticBlur(0);

    actCreateSubThread(actStaff2Demo, 21);
}

void actStaff3Chk(GObj *volatile self)
{
    scpAdpcmPlayRequestFunc(49, &staff3, 0, 1, 1);

    while (staff3 == 0) {
        _ACTWait(1);
    }

    scpFadeIn(6.0f);

    actCreateSubThread(actStaff3Demo, 21);

    actCreateSubThread(actStaff3RollChk, 21);
}

void actStaff3RollChk(GObj *volatile self)
{
    preload(1);

    while (staffRollStartFlag != 0) {
        _ACTWait(1);
    }

    if (ed6 != 0) {
        scpAdpcmFadeCloseFunc(&ed6, 80);
    }

    RequestStageChange(1, boyGObj, 0, 16.0f, 0.001f);
}

void actEndDemo14Chk(GObj *volatile self)
{
    gflagOn(352);

    actCreateSubThread(actConte14_14, 21);
}

static void actEndingSave(GObj *volatile self)
{
    ico_gs_signal(ICO_GS_EV_ENDING, gFlagGameClear);
    if (gFlagGameClear == 0) {
        int save;

        gFlagGameClear = 1;
        save = IosMcPreviewInfo[2];
        gflagInit();
        IosMcPreviewInfo[2] = save;
        gflagOn(395);
        lt_switch_layout(28);
        _ACTWait(60);

        while (current_layout_id != 54) {
            _ACTWait(1);
        }
        gFlagGameClear = 0;
    }

    scpFadeOut(255.0f, 0, 0, 0);

    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    optionScreenMode = 0;
    RequestStageChange(1, boyGObj, 0, 255.0f, 8.0f);
}
