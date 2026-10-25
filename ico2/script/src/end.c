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
#include "ico_gamestate.h" /* port: achievement signals */
#include "ico_credits.h"   /* port: Extras > Credits */

static void actEndingSave(GObj *volatile self);

/* .sdata: the ending scenes' stream handles (ed5 and happy_end unused), then
   the staff roll's, the fourteenth demo's and the st27a ending's. */
SqEntry *ed1 = 0;

SqEntry *ed2 = 0;

SqEntry *ed3 = 0;

SqEntry *ed4 = 0;

SqEntry *ed5 = 0;

SqEntry *ed6 = 0;

SqEntry *sea = 0;

SqEntry *happy_end = 0;

static SqEntry *staff3 = 0; /* derived name */

static SqEntry *endDemo14 = 0; /* derived name */

static SqEntry *st27aEnd = 0; /* derived name */

static ActMail demo01_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail demo01_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail demo01_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define demo01_mes PAL_NTSC_AUG6(demo01_mes_pal, demo01_mes_ntsc, demo01_mes_aug6)

static ActMail demo02_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail demo02_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail demo02_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define demo02_mes PAL_NTSC_AUG6(demo02_mes_pal, demo02_mes_ntsc, demo02_mes_aug6)

static ActMail demo03_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail demo03_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail demo03_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define demo03_mes PAL_NTSC_AUG6(demo03_mes_pal, demo03_mes_ntsc, demo03_mes_aug6)

static ActMail demo04_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail demo04_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail demo04_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define demo04_mes PAL_NTSC_AUG6(demo04_mes_pal, demo04_mes_ntsc, demo04_mes_aug6)

static ActMail demo05_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail demo05_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail demo05_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define demo05_mes PAL_NTSC_AUG6(demo05_mes_pal, demo05_mes_ntsc, demo05_mes_aug6)

static ActMail demo06_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail demo06_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail demo06_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define demo06_mes PAL_NTSC_AUG6(demo06_mes_pal, demo06_mes_ntsc, demo06_mes_aug6)

static ActMail demo07_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail demo07_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail demo07_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define demo07_mes PAL_NTSC_AUG6(demo07_mes_pal, demo07_mes_ntsc, demo07_mes_aug6)

static ActMail demo10_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail demo10_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail demo10_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define demo10_mes PAL_NTSC_AUG6(demo10_mes_pal, demo10_mes_ntsc, demo10_mes_aug6)

static ActMail demo11_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail demo11_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail demo11_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define demo11_mes PAL_NTSC_AUG6(demo11_mes_pal, demo11_mes_ntsc, demo11_mes_aug6)

static ActMail demo12_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail demo12_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail demo12_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define demo12_mes PAL_NTSC_AUG6(demo12_mes_pal, demo12_mes_ntsc, demo12_mes_aug6)

static ActMail demo13_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail demo13_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail demo13_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define demo13_mes PAL_NTSC_AUG6(demo13_mes_pal, demo13_mes_ntsc, demo13_mes_aug6)

static ActMail staff1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail staff1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail staff1_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define staff1_mes PAL_NTSC_AUG6(staff1_mes_pal, staff1_mes_ntsc, staff1_mes_aug6)

static ActMail staff2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail staff2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail staff2_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define staff2_mes PAL_NTSC_AUG6(staff2_mes_pal, staff2_mes_ntsc, staff2_mes_aug6)

static ActMail staff3_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail staff3_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail staff3_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define staff3_mes PAL_NTSC_AUG6(staff3_mes_pal, staff3_mes_ntsc, staff3_mes_aug6)

static ActMail demo14_mes[2] = {{397}, {396}}; /* derived name */

static ActMail ed_demo14_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail ed_demo14_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define ed_demo14_mes PAL_AUG6(ed_demo14_mes_pal, ed_demo14_mes_aug6)

static ActMail st27aEnd_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail st27aEnd_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail st27aEnd_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define st27aEnd_mes PAL_NTSC_AUG6(st27aEnd_mes_pal, st27aEnd_mes_ntsc, st27aEnd_mes_aug6)

static ActMail logo_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail logo_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail logo_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define logo_mes PAL_NTSC_AUG6(logo_mes_pal, logo_mes_ntsc, logo_mes_aug6)

static ActMail end_mes[2] = {{430}, {429}}; /* derived name */

static void actEndDemo01_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(308) == 0 && gflagChk(312) == 0) {
        lt_switch_layout(51);
        scpBoyControlReadDisable = 1;
        scpFadeOut(255.0f, 0, 0, 0);
        stage_SetAnimation(766, 0, 0);
        stage_SetAnimation(31, 0, -1);
        stage_SetAnimation(25, 0, 0);
        stage_SetAnimation(34, 0, -1);
        stage_SetAnimation(32, 0, -1);
        stage_SetAnimation(33, 0, -1);
        demo01_mes[0].func = actEndDemo01Chk;
        act->mail = demo01_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actEndDemo01_aug6(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(307) == 0 && gflagChk(311) == 0) {
        lt_switch_layout(51);
        scpBoyControlReadDisable = 1;
        scpFadeOut(255.0f, 0, 0, 0);
        stage_SetAnimation(765, 0, 0);
        stage_SetAnimation(31, 0, -1);
        stage_SetAnimation(25, 0, 0);
        stage_SetAnimation(34, 0, -1);
        stage_SetAnimation(32, 0, -1);
        stage_SetAnimation(33, 0, -1);
        demo01_mes[0].func = actEndDemo01Chk;
        act->mail = demo01_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actEndDemo01(GObj *volatile self)
{
    if (ICO_NTSC) {
        actEndDemo01_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actEndDemo01_aug6(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(339) == 0 && gflagChk(343) == 0) {
        lt_switch_layout(PAL_JPNKORCN(55, 52));
        scpBoyControlReadDisable = 1;
        scpFadeOut(255.0f, 0, 0, 0);
        stage_SetAnimation(PAL_JPNKORCN(840, 836), 0, 0);
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

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(814, 810, 740, 739), 1, 0);

    scpPlayMot(scpSearchGobj(PAL_NTSC_AUG6(2515, 2307, 2278)), PAL_NTSC_AUG6(809, 764, 766));

    scpSearchGobj(PAL_NTSC_AUG6(2515, 2307, 2278))->active = 1;

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(814, 815), stage_ContinueAnimation(810, 811),
               stage_ContinueAnimation(740, 741), stage_ContinueAnimation(739, 740)) == 0) {
        _ACTWait(1);
    }

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(815, 816), stage_ContinueAnimation(811, 812),
               stage_ContinueAnimation(741, 742), stage_ContinueAnimation(740, 741)) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_NTSCAUG6(55, 52), 1, 0);

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(816, 817), stage_ContinueAnimation(812, 813),
               stage_ContinueAnimation(742, 743), stage_ContinueAnimation(741, 742)) == 0) {
        _ACTWait(1);
    }

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(817, 818), stage_ContinueAnimation(813, 814),
               stage_ContinueAnimation(743, 744), stage_ContinueAnimation(742, 743)) == 0) {
        _ACTWait(1);
    }

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(818, 819), stage_ContinueAnimation(814, 815),
               stage_ContinueAnimation(744, 745), stage_ContinueAnimation(743, 744)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(PAL_NTSC_AUG6(2515, 2307, 2278)), PAL_NTSC_AUG6(810, 765, 767));

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(819, 820), stage_ContinueAnimation(815, 816),
               stage_ContinueAnimation(745, 746), stage_ContinueAnimation(744, 745)) == 0) {
        _ACTWait(1);
    }

    scpSearchGobj(PAL_NTSC_AUG6(2515, 2307, 2278))->active = 0;

    scpPlayMot(scpSearchGobj(PAL_NTSC_AUG6(2516, 2308, 2279)), PAL_NTSC_AUG6(811, 766, 768));

    scpSearchGobj(PAL_NTSC_AUG6(2516, 2308, 2279))->active = 1;

    if (ICO_NTSC) {
        scpLinkBGAtoLayoutedTargetSkeltonWithLocalRotationFlag(2308, 0, 483, 0);
    } else if (ICO_AUG6) {
        scpLinkBGAtoLayoutedTargetSkeltonWithLocalRotationFlag(2279, 0, 482, 0);
    } else {
        scpLinkBGAtoLayoutedTargetSkeltonWithLocalRotationFlag(2516, 0, PAL_JPNKORCN(554, 550), 0);
    }

    stage_SetLoopFlag(PAL_JPNKORCN_NTSC_AUG6(554, 550, 483, 482), 1);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(554, 550, 483, 482), 1, 0);

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(820, 821), stage_ContinueAnimation(816, 817),
               stage_ContinueAnimation(746, 747), stage_ContinueAnimation(745, 746)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(PAL_NTSC_AUG6(2516, 2308, 2279)), PAL_NTSC_AUG6(812, 767, 769));

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(821, 822), stage_ContinueAnimation(817, 818),
               stage_ContinueAnimation(747, 748), stage_ContinueAnimation(746, 747)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(PAL_NTSC_AUG6(2516, 2308, 2279)), PAL_NTSC_AUG6(813, 768, 770));

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(822, 823), stage_ContinueAnimation(818, 819),
               stage_ContinueAnimation(748, 749), stage_ContinueAnimation(747, 748)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(PAL_NTSC_AUG6(2516, 2308, 2279)), PAL_NTSC_AUG6(814, 769, 771));

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6(823, 819, 749, 748)) == 0) {
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

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6(55, 52, 51));
    scpBoyControlReadDisable = 1;
    scpFadeOut(255.0f, 0, 0, 0);
    scpLinkBGAtoKindTargetSkeltonWithLocalRotationFlag(
        3, 0, PAL_JPNKORCN_NTSC_AUG6(554, 550, 483, 482), 0);
    stage_SetLoopFlag(PAL_JPNKORCN_NTSC_AUG6(554, 550, 483, 482), 1);
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(554, 550, 483, 482), 1, 0);
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(825, 821, 751, 750), 0, 0);
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(556, 552, 485, 484), 0, -1);
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(557, 553, 486, 485), 0, -1);
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(559, 555, 488, 487), 0, -1);
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(560, 556, 489, 488), 0, -1);
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(561, 557, 490, 489), 0, -1);
    SelectBoyCrown(boyGObj, 2);
    demo02_mes[0].func = actEndDemo02Chk;
    act->mail = demo02_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

void actConte14_2(GObj *volatile self)
{
    scpPlayStart(boyGObj);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(824, 820, 750, 749), 1, 0);

    scpPlayMot(boyGObj, PAL_NTSCAUG6(441, 435));

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(824, 825), stage_ContinueAnimation(820, 821),
               stage_ContinueAnimation(750, 751), stage_ContinueAnimation(749, 750)) == 0) {
        _ACTWait(1);
    }

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(825, 826), stage_ContinueAnimation(821, 822),
               stage_ContinueAnimation(751, 752), stage_ContinueAnimation(750, 751)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(PAL_NTSC_AUG6(2686, 2470, 2441)), PAL_NTSC_AUG6(815, 770, 772));

    scpSearchGobj(PAL_NTSC_AUG6(2686, 2470, 2441))->active = 1;

    _ACTWait(1);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(827, 823, 753, 752), 1, 0);

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(826, 828), stage_ContinueAnimation(822, 824),
               stage_ContinueAnimation(752, 754), stage_ContinueAnimation(751, 753)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, PAL_NTSCAUG6(442, 436));

    scpPlayMot(scpSearchGobj(PAL_NTSC_AUG6(2686, 2470, 2441)), PAL_NTSC_AUG6(816, 771, 773));

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(828, 829), stage_ContinueAnimation(824, 825),
               stage_ContinueAnimation(754, 755), stage_ContinueAnimation(753, 754)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, PAL_NTSCAUG6(443, 437));

    scpPlayMot(scpSearchGobj(PAL_NTSC_AUG6(2686, 2470, 2441)), PAL_NTSC_AUG6(817, 772, 774));

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(829, 830), stage_ContinueAnimation(825, 826),
               stage_ContinueAnimation(755, 756), stage_ContinueAnimation(754, 755)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, PAL_NTSCAUG6(444, 438));

    scpPlayMot(scpSearchGobj(PAL_NTSC_AUG6(2686, 2470, 2441)), PAL_NTSC_AUG6(818, 773, 775));

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(830, 831), stage_ContinueAnimation(826, 827),
               stage_ContinueAnimation(756, 757), stage_ContinueAnimation(755, 756)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, PAL_NTSCAUG6(445, 439));

    scpPlayMot(scpSearchGobj(PAL_NTSC_AUG6(2686, 2470, 2441)), PAL_NTSC_AUG6(819, 774, 776));

    _ACTWait(1);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(832, 828, 758, 757), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6(831, 827, 757, 756)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayEnd(boyGObj);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6(54, 51, 50));

    RequestStageChange(3, boyGObj, 0, 1.0f, 8.0f);
}

static void actEndDemo06_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(313) == 0 && gflagChk(312) != 0) {
        lt_switch_layout(51);
        scpBoyControlReadDisable = 1;
        scpLinkBGAtoLayoutedTargetSkeltonWithLocalRotationFlag(2308, 0, 483, 0);
        stage_SetLoopFlag(483, 1);
        stage_SetAnimation(483, 1, 0);
        stage_SetAnimation(766, 0, 0);
        stage_SetAnimation(31, 0, -1);
        stage_SetAnimation(25, 0, 0);
        stage_SetAnimation(34, 0, -1);
        stage_SetAnimation(32, 0, -1);
        stage_SetAnimation(33, 0, -1);
        SelectBoyCrown(boyGObj, 2);
        demo06_mes[0].func = actEndDemo06Chk;
        act->mail = demo06_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actEndDemo06_aug6(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(312) == 0 && gflagChk(311) != 0) {
        lt_switch_layout(51);
        scpBoyControlReadDisable = 1;
        scpLinkBGAtoLayoutedTargetSkeltonWithLocalRotationFlag(2279, 0, 482, 0);
        stage_SetLoopFlag(482, 1);
        stage_SetAnimation(482, 1, 0);
        stage_SetAnimation(765, 0, 0);
        stage_SetAnimation(31, 0, -1);
        stage_SetAnimation(25, 0, 0);
        stage_SetAnimation(34, 0, -1);
        stage_SetAnimation(32, 0, -1);
        stage_SetAnimation(33, 0, -1);
        SelectBoyCrown(boyGObj, 2);
        demo06_mes[0].func = actEndDemo06Chk;
        act->mail = demo06_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actEndDemo06(GObj *volatile self)
{
    if (ICO_NTSC) {
        actEndDemo06_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actEndDemo06_aug6(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(344) == 0 && gflagChk(343) != 0) {
        lt_switch_layout(PAL_JPNKORCN(55, 52));
        scpBoyControlReadDisable = 1;
        scpFadeOut(255.0f, 0, 0, 0);
        scpLinkBGAtoLayoutedTargetSkeltonWithLocalRotationFlag(2516, 0, PAL_JPNKORCN(554, 550), 0);
        stage_SetLoopFlag(PAL_JPNKORCN(554, 550), 1);
        stage_SetAnimation(PAL_JPNKORCN(554, 550), 1, 0);
        stage_SetAnimation(PAL_JPNKORCN(840, 836), 0, 0);
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

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(838, 834, 764, 763), 1, 0);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(840, 836, 766, 765), 1, 0);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(841, 837, 767, 766), 1, 0);

    scpPlayMot(boyGObj, PAL_NTSCAUG6(446, 440));

    scpPlayMot(scpSearchGobj(PAL_NTSC_AUG6(2516, 2308, 2279)), PAL_NTSC_AUG6(820, 775, 777));

    scpSearchGobj(PAL_NTSC_AUG6(2516, 2308, 2279))->active = 1;

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(838, 839), stage_ContinueAnimation(834, 835),
               stage_ContinueAnimation(764, 765), stage_ContinueAnimation(763, 764)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, PAL_NTSCAUG6(447, 441));

    scpPlayMot(scpSearchGobj(PAL_NTSC_AUG6(2516, 2308, 2279)), PAL_NTSC_AUG6(821, 776, 778));

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6(839, 835, 765, 764)) == 0) {
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

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6(55, 52, 51));
    scpBoyControlReadDisable = 1;
    scpFadeOut(255.0f, 0, 0, 0);
    scpLinkBGAtoKindTargetSkeltonWithLocalRotationFlag(
        3, 0, PAL_JPNKORCN_NTSC_AUG6(554, 550, 483, 482), 0);
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(173, 169, 152, 151), 0, 0);
    stage_SetLoopFlag(PAL_JPNKORCN_NTSC_AUG6(554, 550, 483, 482), 1);
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(554, 550, 483, 482), 1, 0);

    if (gflagChk(PAL_NTSC_AUG6(345, 314, 313)) == 0) {
        SelectBoyCrown(boyGObj, 2);
        demo07_mes[0].func = actEndDemo07Chk;
        act->mail = demo07_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
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

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(842, 838, 768, 767), 1, 0);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(173, 169, 152, 151), 1, 471);

    _ACTWait(1);

    scpPlayPosSet(boyGObj, -4757.0f, -660.0f, 2646.0f);

    scpPlayPosSet(scpSearchGobj(PAL_NTSC_AUG6(2388, 2181, 2156)), -4757.0f, -664.0f, 2646.0f);

    _ACTWait(1);

    ofs[0] = conte14_7Pos.d[0];
    ofs[1] = conte14_7Pos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    ofs[0] = conte14_7Pos.d[0];
    ofs[1] = conte14_7Pos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(scpSearchGobj(PAL_NTSC_AUG6(2388, 2181, 2156))));
    scpPlayMotDir(scpSearchGobj(PAL_NTSC_AUG6(2388, 2181, 2156)), dir);

    scpPlayMotNode(boyGObj, PAL_NTSCAUG6(448, 442), scpSearchGobj(PAL_NTSC_AUG6(2388, 2181, 2156)),
                   44);

    scpPlayMot(scpSearchGobj(PAL_NTSC_AUG6(2388, 2181, 2156)), PAL_NTSC_AUG6(822, 777, 779));

    scpSearchGobj(PAL_NTSC_AUG6(2388, 2181, 2156))->active = 1;

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6(842, 838, 768, 767)) == 0) {
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

    if (ICO_AUG6) {
        lt_switch_layout(51);
        scpBoyControlReadDisable = 1;
        scpFadeOut(255.0f, 0, 0, 0);
        scpLinkBGAtoKindTargetSkeltonWithLocalRotationFlag(3, 0, 482, 0);
        stage_SetLoopFlag(482, 1);
        stage_SetAnimation(482, 1, 0);
        SelectBoyCrown(boyGObj, 2);
        stage_SetAnimation(775, 0, 0);
        demo10_mes[0].func = actEndDemo10Chk;
        act->mail = demo10_mes;
        ACTSendMailCorrect(self, 393);
    } else if (ICO_NTSC) {
        lt_switch_layout(51);
        scpBoyControlReadDisable = 1;
        scpFadeOut(255.0f, 0, 0, 0);
        scpLinkBGAtoKindTargetSkeltonWithLocalRotationFlag(3, 0, 483, 0);
        stage_SetLoopFlag(483, 1);
        stage_SetAnimation(483, 1, 0);
        SelectBoyCrown(boyGObj, 2);
        stage_SetAnimation(776, 0, 0);
        demo10_mes[0].func = actEndDemo10Chk;
        act->mail = demo10_mes;
        ACTSendMailCorrect(self, 397);
    } else if (ICO_JPNKORCN) {
        lt_switch_layout(52);
        scpBoyControlReadDisable = 1;
        scpFadeOut(255.0f, 0, 0, 0);
        scpLinkBGAtoKindTargetSkeltonWithLocalRotationFlag(3, 0, 550, 0);
        scpLinkBGAtoKindTargetSkeltonWithLocalRotationFlag(3, 52, 58, 0);
        stage_SetLoopFlag(550, 1);
        stage_SetAnimation(550, 1, 0);
        SelectBoyCrown(boyGObj, 2);
        stage_SetAnimation(846, 0, 0);
        demo10_mes[0].func = actEndDemo10Chk;
        act->mail = demo10_mes;
        ACTSendMailCorrect(self, 430);
    } else {
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
    }
    _ACTWait(0);
}

static void actConte14_10_ntsc(GObj *volatile self)
{
    preload(3);

    scpPlayStart(boyGObj);

    stage_SetAnimation(769, 1, 0);

    scpPlayMot(boyGObj, 443);

    scpPlayMot(scpSearchGobj(1940), 778);

    scpSearchGobj(1940)->active = 1;

    _ACTWait(1);

    stage_SetAnimation(42, 1, 0);

    stage_SetAnimation(770, 1, 0);

    while (stage_ContinueAnimation(769, 771) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 444);

    scpPlayMot(scpSearchGobj(1940), 779);

    _ACTWait(1);

    stage_SetAnimation(43, 1, 0);

    while (stage_ContinueAnimation(771, 772) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 445);

    scpPlayMot(scpSearchGobj(1940), 780);

    _ACTWait(1);

    stage_SetAnimation(44, 1, 0);

    while (stage_ContinueAnimation(772, 773) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 446);

    scpPlayMot(scpSearchGobj(1940), 781);

    _ACTWait(1);

    stage_SetAnimation(45, 1, 0);

    while (stage_ContinueAnimation(773, 774) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 447);

    scpPlayMot(scpSearchGobj(1940), 782);

    _ACTWait(1);

    stage_SetAnimation(46, 1, 0);

    while (stage_ContinueAnimation(774, 775) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 448);

    _ACTWait(1);

    stage_SetAnimation(47, 1, 0);

    stage_SetAnimation(53, 1, 0);

    stage_SetAnimation(776, 1, 0);

    while (stage_ContinueAnimation(775, 777) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 449);

    _ACTWait(1);

    stage_SetAnimation(48, 1, 0);

    stage_SetAnimation(54, 1, 0);

    while (stage_CheckAnimationFinish(777) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    RequestStageChange(3, boyGObj, girlGObj, 1.0f, 8.0f);
}

static void actConte14_10_aug6(GObj *volatile self)
{
    preload(3);

    scpPlayStart(boyGObj);

    stage_SetAnimation(768, 1, 0);

    scpPlayMot(boyGObj, 443);

    scpPlayMot(scpSearchGobj(1916), 780);

    scpSearchGobj(1916)->active = 1;

    _ACTWait(1);

    stage_SetAnimation(42, 1, 0);

    stage_SetAnimation(769, 1, 0);

    while (stage_ContinueAnimation(768, 770) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 444);

    scpPlayMot(scpSearchGobj(1916), 781);

    _ACTWait(1);

    stage_SetAnimation(43, 1, 0);

    while (stage_ContinueAnimation(770, 771) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 445);

    scpPlayMot(scpSearchGobj(1916), 782);

    _ACTWait(1);

    stage_SetAnimation(44, 1, 0);

    while (stage_ContinueAnimation(771, 772) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 446);

    scpPlayMot(scpSearchGobj(1916), 783);

    _ACTWait(1);

    stage_SetAnimation(45, 1, 0);

    while (stage_ContinueAnimation(772, 773) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 447);

    scpPlayMot(scpSearchGobj(1916), 784);

    _ACTWait(1);

    stage_SetAnimation(46, 1, 0);

    while (stage_ContinueAnimation(773, 774) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 448);

    _ACTWait(1);

    stage_SetAnimation(47, 1, 0);

    stage_SetAnimation(53, 1, 0);

    stage_SetAnimation(775, 1, 0);

    while (stage_ContinueAnimation(774, 776) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 449);

    _ACTWait(1);

    stage_SetAnimation(48, 1, 0);

    stage_SetAnimation(54, 1, 0);

    while (stage_CheckAnimationFinish(776) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    RequestStageChange(3, boyGObj, girlGObj, 1.0f, 8.0f);
}

void actConte14_10(GObj *volatile self)
{
    if (ICO_NTSC) {
        actConte14_10_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actConte14_10_aug6(self);
        return;
    }
    preload(3);

    scpPlayStart(boyGObj);

    stage_SetAnimation(PAL_JPNKORCN(843, 839), 1, 0);

    scpPlayMot(boyGObj, 449);

    scpPlayMot(scpSearchGobj(2122), 823);

    scpSearchGobj(2122)->active = 1;

    _ACTWait(1);

    stage_SetAnimation(45, 1, 0);

    stage_SetAnimation(PAL_JPNKORCN(844, 840), 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(843, 839), PAL_JPNKORCN(845, 841)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 450);

    scpPlayMot(scpSearchGobj(2122), 824);

    _ACTWait(1);

    stage_SetAnimation(46, 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(845, 841), PAL_JPNKORCN(846, 842)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 451);

    scpPlayMot(scpSearchGobj(2122), 825);

    _ACTWait(1);

    stage_SetAnimation(47, 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(846, 842), PAL_JPNKORCN(847, 843)) == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(ed6->stream);

    scpPlayMot(boyGObj, 452);

    scpPlayMot(scpSearchGobj(2122), 826);

    _ACTWait(1);

    stage_SetAnimation(48, 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(847, 843), PAL_JPNKORCN(848, 844)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 453);

    scpPlayMot(scpSearchGobj(2122), 827);

    _ACTWait(1);

    stage_SetAnimation(49, 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(848, 844), PAL_JPNKORCN(849, 845)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 454);

    _ACTWait(1);

    stage_SetAnimation(50, 1, 0);

    stage_SetAnimation(56, 1, 0);

    stage_SetAnimation(PAL_JPNKORCN(850, 846), 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(849, 845), PAL_JPNKORCN(851, 847)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 455);

    _ACTWait(1);

    stage_SetAnimation(51, 1, 0);

    stage_SetAnimation(57, 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(851, 847)) == 0) {
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

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(854, 850, 780, 779), 1, 0);

    scpPlayMot(boyGObj, PAL_NTSCAUG6(456, 450));

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(854, 855), stage_ContinueAnimation(850, 851),
               stage_ContinueAnimation(780, 781), stage_ContinueAnimation(779, 780)) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_NTSCAUG6(53, 50), 1, 0);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(465, 461, 402, 401), 1, 0);

    scpPlayMot(boyGObj, PAL_NTSCAUG6(456, 450));

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6(855, 851, 781, 780)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpPlayEnd(boyGObj);

    RequestStageChange(6, boyGObj, girlGObj, 1.0f, 8.0f);
}

static void actStaff1_aug6(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    stage_SetAnimation(29, 0, 0);
    stage_SetAnimation(25, 0, 0);
    stage_SetAnimation(32, 0, 0);
    stage_SetAnimation(33, 0, 0);
    stage_SetAnimation(35, 0, 0);
    stage_SetAnimation(37, 0, 0);
    staff1_mes[0].func = actStaff1Chk;
    act->mail = staff1_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

static void actStaff1_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    stage_SetAnimation(29, 0, 0);
    stage_SetAnimation(25, 0, 0);
    stage_SetAnimation(32, 0, 0);
    stage_SetAnimation(33, 0, 0);
    stage_SetAnimation(35, 0, 0);
    stage_SetAnimation(37, 0, 0);
    staff1_mes[0].func = actStaff1Chk;
    act->mail = staff1_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

void actStaff1(GObj *volatile self)
{
    if (ICO_AUG6) {
        actStaff1_aug6(self);
        return;
    }
    if (ICO_NTSC) {
        actStaff1_ntsc(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6(55, 52, 51));
    scpBoyControlReadDisable = 1;
    if (ICO_NTSCAUG6) {
        stage_SetAnimation(29, 0, 0);
        stage_SetAnimation(25, 0, 0);
        stage_SetAnimation(32, 0, 0);
    } else {
        stage_SetAnimation(30, 0, -1);
        stage_SetAnimation(26, 0, 0);
    }
    stage_SetAnimation(33, 0, 0);
    if (!ICO_NTSCAUG6) {
        stage_SetAnimation(34, 0, 0);
        stage_SetAnimation(36, 0, 0);
        stage_SetAnimation(38, 0, 0);
    }
    stage_SetAnimation(35, 0, 0);
    staff1_mes[0].func = actStaff1Chk;
    act->mail = staff1_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

static void actStaff1Demo_ntsc(GObj *volatile self)
{
    preload(1);

    staffRollStart(1.0f, 255);

    stage_SetAnimation(523, 1, 0);

    while (stage_ContinueAnimation(523, 524) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(25, 1, 0);

    scpPlayMot(boyGObj, 282);

    while (stage_ContinueAnimation(524, 526) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(526, 527) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 283);

    _ACTWait(130);

    stage_SetAnimation(31, 1, 0);

    while (stage_ContinueAnimation(527, 528) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(528, 529) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 284);

    while (stage_ContinueAnimation(529, 530) == 0) {
        _ACTWait(1);
    }

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

    while (stage_ContinueAnimation(535, 536) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 291);

    while (stage_CheckAnimationFrame(536, 100, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpFadeOut(8.0f, 0, 0, 0);

    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    RequestStageChange(1, boyGObj, girlGObj, 0.0f, 8.0f);
}

static void actStaff1Demo_aug6(GObj *volatile self)
{
    preload(1);

    staffRollStart(1.0f, 255);

    stage_SetAnimation(522, 1, 0);

    while (stage_ContinueAnimation(522, 523) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(25, 1, 0);

    scpPlayMot(boyGObj, 282);

    while (stage_ContinueAnimation(523, 525) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(525, 526) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 283);

    _ACTWait(130);

    stage_SetAnimation(31, 1, 0);

    while (stage_ContinueAnimation(526, 527) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(527, 528) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 284);

    while (stage_ContinueAnimation(528, 529) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(529, 530) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 286);

    while (stage_ContinueAnimation(530, 531) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 287);

    while (stage_ContinueAnimation(531, 532) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 288);

    while (stage_ContinueAnimation(532, 533) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 289);

    while (stage_ContinueAnimation(533, 534) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 290);

    while (stage_ContinueAnimation(534, 535) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 291);

    while (stage_CheckAnimationFrame(535, 100, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpFadeOut(8.0f, 0, 0, 0);

    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    RequestStageChange(1, boyGObj, girlGObj, 0.0f, 8.0f);
}

void actStaff1Demo(GObj *volatile self)
{
    if (ICO_NTSC) {
        actStaff1Demo_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actStaff1Demo_aug6(self);
        return;
    }
    preload(1);

    staffRollStart(1.0f, 255);

    stage_SetAnimation(PAL_JPNKORCN(870, 866), 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(870, 866), PAL_JPNKORCN(871, 867)) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(26, 1, 0);

    scpPlayMot(boyGObj, 288);

    while (stage_ContinueAnimation(PAL_JPNKORCN(871, 867), PAL_JPNKORCN(873, 869)) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(PAL_JPNKORCN(873, 869), PAL_JPNKORCN(874, 870)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 289);

    _ACTWait(130);

    stage_SetAnimation(32, 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKORCN(874, 870), PAL_JPNKORCN(875, 871)) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(PAL_JPNKORCN(875, 871), PAL_JPNKORCN(876, 872)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 290);

    while (stage_ContinueAnimation(PAL_JPNKORCN(876, 872), PAL_JPNKORCN(877, 873)) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(PAL_JPNKORCN(877, 873), PAL_JPNKORCN(878, 874)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 292);

    _ACTWait(1);

    stage_SetAnimation(32, 1, 208);

    while (stage_ContinueAnimation(PAL_JPNKORCN(878, 874), PAL_JPNKORCN(879, 875)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 293);

    while (stage_ContinueAnimation(PAL_JPNKORCN(879, 875), PAL_JPNKORCN(880, 876)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 294);

    while (stage_ContinueAnimation(PAL_JPNKORCN(880, 876), PAL_JPNKORCN(881, 877)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 295);

    while (stage_ContinueAnimation(PAL_JPNKORCN(881, 877), PAL_JPNKORCN(882, 878)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 296);

    while (stage_ContinueAnimation(PAL_JPNKORCN(882, 878), PAL_JPNKORCN(883, 879)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 297);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(883, 879), 100, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpFadeOut(8.0f, 0, 0, 0);

    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    RequestStageChange(1, boyGObj, girlGObj, 0.0f, 8.0f);
}

static void actStaff2Demo_ntsc(GObj *volatile self)
{
    preload(1);

    stage_SetAnimation(577, 1, 0);

    stage_SetAnimation(238, 1, 0);

    scpPlayMot(boyGObj, 321);

    scpPlayMot(scpSearchGobj(2585), 692);

    scpSearchGobj(2585)->active = 1;

    while (stage_ContinueAnimation(577, 578) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 322);

    scpPlayMot(scpSearchGobj(2585), 693);

    while (stage_ContinueAnimation(578, 579) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 323);

    scpPlayMot(scpSearchGobj(2585), 694);

    while (stage_ContinueAnimation(579, 580) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(580, 581) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 324);

    scpPlayMot(scpSearchGobj(2585), 695);

    while (stage_ContinueAnimation(581, 582) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 325);

    scpPlayMot(scpSearchGobj(2585), 696);

    while (stage_ContinueAnimation(582, 583) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 326);

    scpPlayMot(scpSearchGobj(2585), 697);

    while (stage_ContinueAnimation(583, 584) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 327);

    while (stage_ContinueAnimation(584, 599) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 335);

    scpPlayMot(scpSearchGobj(2585), 703);

    while (stage_ContinueAnimation(599, 600) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 336);

    scpPlayMot(scpSearchGobj(2585), 704);

    while (stage_ContinueAnimation(600, 601) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 337);

    scpPlayMot(scpSearchGobj(2585), 705);

    while (stage_ContinueAnimation(601, 602) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 338);

    scpPlayMot(scpSearchGobj(2585), 706);

    while (stage_CheckAnimationFrame(602, 200, 0) == 0) {
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

static void actStaff2Demo_aug6(GObj *volatile self)
{
    preload(1);

    stage_SetAnimation(576, 1, 0);

    stage_SetAnimation(237, 1, 0);

    scpPlayMot(boyGObj, 321);

    scpPlayMot(scpSearchGobj(2556), 694);

    scpSearchGobj(2556)->active = 1;

    while (stage_ContinueAnimation(576, 577) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 322);

    scpPlayMot(scpSearchGobj(2556), 695);

    while (stage_ContinueAnimation(577, 578) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 323);

    scpPlayMot(scpSearchGobj(2556), 696);

    while (stage_ContinueAnimation(578, 579) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(579, 580) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 324);

    scpPlayMot(scpSearchGobj(2556), 697);

    while (stage_ContinueAnimation(580, 581) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 325);

    scpPlayMot(scpSearchGobj(2556), 698);

    while (stage_ContinueAnimation(581, 582) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 326);

    scpPlayMot(scpSearchGobj(2556), 699);

    while (stage_ContinueAnimation(582, 583) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 327);

    while (stage_ContinueAnimation(583, 598) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 335);

    scpPlayMot(scpSearchGobj(2556), 705);

    while (stage_ContinueAnimation(598, 599) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 336);

    scpPlayMot(scpSearchGobj(2556), 706);

    while (stage_ContinueAnimation(599, 600) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 337);

    scpPlayMot(scpSearchGobj(2556), 707);

    while (stage_ContinueAnimation(600, 601) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 338);

    scpPlayMot(scpSearchGobj(2556), 708);

    while (stage_CheckAnimationFrame(601, 200, 0) == 0) {
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

void actStaff2Demo(GObj *volatile self)
{
    if (ICO_NTSC) {
        actStaff2Demo_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actStaff2Demo_aug6(self);
        return;
    }
    preload(1);

    stage_SetAnimation(PAL_JPNKORCN(907, 903), 1, 0);

    stage_SetAnimation(PAL_JPNKORCN(269, 265), 1, 0);

    scpPlayMot(boyGObj, 327);

    scpPlayMot(scpSearchGobj(2789), 737);

    scpSearchGobj(2789)->active = 1;

    while (stage_ContinueAnimation(PAL_JPNKORCN(907, 903), PAL_JPNKORCN(908, 904)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 328);

    scpPlayMot(scpSearchGobj(2789), 738);

    while (stage_ContinueAnimation(PAL_JPNKORCN(908, 904), PAL_JPNKORCN(909, 905)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 329);

    scpPlayMot(scpSearchGobj(2789), 739);

    while (stage_ContinueAnimation(PAL_JPNKORCN(909, 905), PAL_JPNKORCN(910, 906)) == 0) {
        _ACTWait(1);
    }

    while (stage_ContinueAnimation(PAL_JPNKORCN(910, 906), PAL_JPNKORCN(911, 907)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 330);

    scpPlayMot(scpSearchGobj(2789), 740);

    while (stage_ContinueAnimation(PAL_JPNKORCN(911, 907), PAL_JPNKORCN(912, 908)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 331);

    scpPlayMot(scpSearchGobj(2789), 741);

    while (stage_ContinueAnimation(PAL_JPNKORCN(912, 908), PAL_JPNKORCN(913, 909)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 332);

    scpPlayMot(scpSearchGobj(2789), 742);

    while (stage_ContinueAnimation(PAL_JPNKORCN(913, 909), PAL_JPNKORCN(914, 910)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 333);

    while (stage_ContinueAnimation(PAL_JPNKORCN(914, 910), PAL_JPNKORCN(929, 925)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 341);

    scpPlayMot(scpSearchGobj(2789), 748);

    while (stage_ContinueAnimation(PAL_JPNKORCN(929, 925), PAL_JPNKORCN(930, 926)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 342);

    scpPlayMot(scpSearchGobj(2789), 749);

    while (stage_ContinueAnimation(PAL_JPNKORCN(930, 926), PAL_JPNKORCN(931, 927)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 343);

    scpPlayMot(scpSearchGobj(2789), 750);

    while (stage_ContinueAnimation(PAL_JPNKORCN(931, 927), PAL_JPNKORCN(932, 928)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 344);

    scpPlayMot(scpSearchGobj(2789), 751);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(932, 928), 200, 0) == 0) {
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

static void actStaff3Demo_ntsc(GObj *volatile self)
{
    long long ofs[2];
    float dir[4];

    stage_SetAnimation(554, 1, 0);

    scpPlayPosSet(scpSearchGobj(2637), -7.0f, -5725.0f, 18.0f);

    ofs[0] = staff3DemoPos.d[0];
    ofs[1] = staff3DemoPos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(scpSearchGobj(2637)));
    scpPlayMotDir(scpSearchGobj(2637), dir);

    scpPlayMot(scpSearchGobj(2637), 633);

    scpSearchGobj(2637)->active = 1;

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

    while (stage_ContinueAnimation(557, 565) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(61, 0, -1);

    scpPlayMot(scpSearchGobj(2637), 683);

    while (stage_ContinueAnimation(565, 566) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(2637), 684);

    while (stage_ContinueAnimation(566, 567) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 312);

    scpPlayMot(scpSearchGobj(2637), 685);

    while (stage_ContinueAnimation(567, 568) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 313);

    scpPlayMot(scpSearchGobj(2637), 686);

    while (stage_ContinueAnimation(568, 570) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 314);

    scpPlayMot(scpSearchGobj(2637), 687);

    while (stage_CheckAnimationFrame(570, 200, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpFadeOut(6.0f, 0, 0, 0);
}

static void actStaff3Demo_aug6(GObj *volatile self)
{
    long long ofs[2];
    float dir[4];

    stage_SetAnimation(553, 1, 0);

    scpPlayPosSet(scpSearchGobj(2607), -7.0f, -5725.0f, 18.0f);

    ofs[0] = staff3DemoPos.d[0];
    ofs[1] = staff3DemoPos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(scpSearchGobj(2607)));
    scpPlayMotDir(scpSearchGobj(2607), dir);

    scpPlayMot(scpSearchGobj(2607), 635);

    scpSearchGobj(2607)->active = 1;

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

    while (stage_ContinueAnimation(556, 564) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(61, 0, -1);

    scpPlayMot(scpSearchGobj(2607), 685);

    while (stage_ContinueAnimation(564, 565) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(2607), 686);

    while (stage_ContinueAnimation(565, 566) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 312);

    scpPlayMot(scpSearchGobj(2607), 687);

    while (stage_ContinueAnimation(566, 567) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 313);

    scpPlayMot(scpSearchGobj(2607), 688);

    while (stage_ContinueAnimation(567, 569) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 314);

    scpPlayMot(scpSearchGobj(2607), 689);

    while (stage_CheckAnimationFrame(569, 200, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpFadeOut(6.0f, 0, 0, 0);
}

void actStaff3Demo(GObj *volatile self)
{
    if (ICO_NTSC) {
        actStaff3Demo_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actStaff3Demo_aug6(self);
        return;
    }
    long long ofs[2];
    float dir[4];

    stage_SetAnimation(PAL_JPNKORCN(884, 880), 1, 0);

    scpPlayPosSet(scpSearchGobj(2841), -7.0f, -5725.0f, 18.0f);

    ofs[0] = staff3DemoPos.d[0];
    ofs[1] = staff3DemoPos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(scpSearchGobj(2841)));
    scpPlayMotDir(scpSearchGobj(2841), dir);

    scpPlayMot(scpSearchGobj(2841), 551);

    scpSearchGobj(2841)->active = 1;

    scpPlayMot(boyGObj, 308);

    while (stage_ContinueAnimation(PAL_JPNKORCN(884, 880), PAL_JPNKORCN(885, 881)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 309);

    while (stage_ContinueAnimation(PAL_JPNKORCN(885, 881), PAL_JPNKORCN(886, 882)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 310);

    while (stage_ContinueAnimation(PAL_JPNKORCN(886, 882), PAL_JPNKORCN(887, 883)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 311);

    while (stage_ContinueAnimation(PAL_JPNKORCN(887, 883), PAL_JPNKORCN(895, 891)) == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JPNKORCN(74, 70), 0, -1);

    scpPlayMot(boyGObj, 318);

    scpPlayMot(scpSearchGobj(2841), 728);

    while (stage_ContinueAnimation(PAL_JPNKORCN(895, 891), PAL_JPNKORCN(896, 892)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 318);

    scpPlayMot(scpSearchGobj(2841), 729);

    while (stage_ContinueAnimation(PAL_JPNKORCN(896, 892), PAL_JPNKORCN(897, 893)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 318);

    scpPlayMot(scpSearchGobj(2841), 730);

    while (stage_ContinueAnimation(PAL_JPNKORCN(897, 893), PAL_JPNKORCN(898, 894)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 319);

    scpPlayMot(scpSearchGobj(2841), 731);

    while (stage_ContinueAnimation(PAL_JPNKORCN(898, 894), PAL_JPNKORCN(900, 896)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 320);

    scpPlayMot(scpSearchGobj(2841), 732);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(900, 896), 200, 0) == 0) {
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

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6(55, 52, 51));
    scpBoyControlReadDisable = 1;
    fbKeep = 1;
    scpFadeOut(3.0f, 255, 255, 255);

    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    fbKeep = 0;
    ScpCallCameraSetTarget(7046.0f, -77.0f, 1678.0f);

    if (ICO_NTSC) {
        scpAdpcmPlayRequestFunc(93, &sea, 1, 0, 1);

        while (sea == 0) {
            _ACTWait(1);
        }
    } else if (ICO_AUG6) {
        scpAdpcmPlayRequestFunc(93, &sea, 1, 0, 1);
    } else {
        scpAdpcmPlayRequestFunc(95, &endDemo14, 1, 0, 1);

        while (endDemo14 == 0) {
            _ACTWait(1);
        }
    }

    SelectBoyCrown(boyGObj, 2);
    if (ICO_NTSC) {
        demo14_mes[0].func = actEndDemo14Chk;
        act->mail = demo14_mes;
        ACTSendMailCorrect(self, 397);
    } else if (ICO_AUG6) {
        ed_demo14_mes[0].func = actEndDemo14Chk;
        act->mail = ed_demo14_mes;
        ACTSendMailCorrect(self, 393);
    } else {
        SetGirlClothDispSwitch(scpSearchGobj(2253), 1, 2);
        ed_demo14_mes[0].func = actEndDemo14Chk;
        act->mail = ed_demo14_mes;
        ACTSendMailCorrect(self, 430);
    }
    _ACTWait(0);
}

void actConte14_14(GObj *volatile self)
{
    long long ofs[2];
    float dir[4];

    scpPlayStart(boyGObj);

    scpFadeIn(6.0f);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(860, 856, 786, 785), 1, 0);

    scpPlayMot(boyGObj, PAL_NTSCAUG6(461, 455));

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(860, 861), stage_ContinueAnimation(856, 857),
               stage_ContinueAnimation(786, 787), stage_ContinueAnimation(785, 786)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, PAL_NTSCAUG6(462, 456));

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(861, 862), stage_ContinueAnimation(857, 858),
               stage_ContinueAnimation(787, 788), stage_ContinueAnimation(786, 787)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, PAL_NTSCAUG6(463, 457));

    _ACTWait(1);

    stage_SetAnimation(PAL_NTSCAUG6(54, 51), 1, 0);

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(862, 863), stage_ContinueAnimation(858, 859),
               stage_ContinueAnimation(788, 789), stage_ContinueAnimation(787, 788)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, PAL_NTSCAUG6(464, 458));

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(863, 864), stage_ContinueAnimation(859, 860),
               stage_ContinueAnimation(789, 790), stage_ContinueAnimation(788, 789)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, PAL_NTSCAUG6(465, 459));

    while (PAL_JPNKORCN_NTSC_AUG6(
               stage_ContinueAnimation(864, 865), stage_ContinueAnimation(860, 861),
               stage_ContinueAnimation(790, 791), stage_ContinueAnimation(789, 790)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, PAL_NTSCAUG6(466, 460));

    scpPlayMot(scpSearchGobj(PAL_NTSC_AUG6(2253, 2067, 2042)), PAL_NTSC_AUG6(829, 784, 786));

    scpSearchGobj(PAL_NTSC_AUG6(2253, 2067, 2042))->active = 1;

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6(865, 861, 791, 790)) == 0) {
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

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6(54, 51, 50));

    gflagOff(PAL_NTSC_AUG6(338, 307, 306));
}

static void actSt27aEnd_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(323) == 0) {
        st27aEnd_mes[0].func = actSt27aEndChk;
        act->mail = st27aEnd_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actSt27aEnd_aug6(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    if (gflagChk(322) == 0) {
        st27aEnd_mes[0].func = actSt27aEndChk;
        act->mail = st27aEnd_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt27aEnd(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt27aEnd_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt27aEnd_aug6(self);
        return;
    }
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

static void actSt27aEndChk_ntsc(GObj *volatile self)
{
    float max;
    float t;

    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    scpPlayStart(boyGObj);
    scpPlayMot(boyGObj, 0);

    if (sea != 0) {
        scpAdpcmFadeClose(sea, 27);
    }
    scpAdpcmPlayRequestFunc(49, &happy_end, 0, 1, 1);
    while (happy_end == 0) {
        _ACTWait(1);
    }

    gflagOn(323);

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

static void actSt27aEndChk_aug6(GObj *volatile self)
{
    float max;
    float t;

    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    scpPlayStart(boyGObj);
    scpPlayMot(boyGObj, 0);

    if (sea != 0) {
        scpAdpcmFadeClose(sea, 27);
    }
    scpAdpcmPlayRequestFunc(49, &happy_end, 0, 1, 1);
    while (happy_end == 0) {
        _ACTWait(1);
    }

    gflagOn(322);

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

void actSt27aEndChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt27aEndChk_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt27aEndChk_aug6(self);
        return;
    }
    float max;
    float t;

    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
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

static void actSt27aEndDemo_ntsc(GObj *volatile self)
{
    stage_SetAnimation(792, 1, 0);

    scpPlayMot(boyGObj, 461);

    scpPlayMot(scpSearchGobj(2067), 785);

    scpSearchGobj(2067)->active = 1;

    while (stage_ContinueAnimation(792, 793) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 462);

    scpPlayMot(scpSearchGobj(2067), 786);

    while (stage_ContinueAnimation(793, 794) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 463);

    scpPlayMot(scpSearchGobj(2067), 787);

    while (stage_CheckAnimationFinish(794) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    RequestStageChange(1, boyGObj, girlGObj, 1.0f, 8.0f);
}

static void actSt27aEndDemo_aug6(GObj *volatile self)
{
    stage_SetAnimation(791, 1, 0);

    scpPlayMot(boyGObj, 461);

    scpPlayMot(scpSearchGobj(2042), 787);

    scpSearchGobj(2042)->active = 1;

    while (stage_ContinueAnimation(791, 792) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 462);

    scpPlayMot(scpSearchGobj(2042), 788);

    while (stage_ContinueAnimation(792, 793) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 463);

    scpPlayMot(scpSearchGobj(2042), 789);

    while (stage_CheckAnimationFinish(793) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    RequestStageChange(1, boyGObj, girlGObj, 1.0f, 8.0f);
}

void actSt27aEndDemo(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt27aEndDemo_ntsc(self);
        return;
    }
    if (ICO_AUG6) {
        actSt27aEndDemo_aug6(self);
        return;
    }
    stage_SetAnimation(PAL_JPNKORCN(866, 862), 1, 0);

    if (gflagChk(354) == 0) {
        scpPlayMot(boyGObj, 467);
    } else {
        scpPlayMot(boyGObj, 470);
    }

    scpPlayMot(scpSearchGobj(2253), 830);

    scpSearchGobj(2253)->active = 1;

    while (stage_ContinueAnimation(PAL_JPNKORCN(866, 862), PAL_JPNKORCN(867, 863)) == 0) {
        _ACTWait(1);
    }

    if (gflagChk(354) == 0) {
        scpPlayMot(boyGObj, 468);
    } else {
        scpPlayMot(boyGObj, 471);
    }

    scpPlayMot(scpSearchGobj(2253), 831);

    while (stage_ContinueAnimation(PAL_JPNKORCN(867, 863), PAL_JPNKORCN(868, 864)) == 0) {
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
        while (stage_CheckAnimationFinish(PAL_JPNKORCN(868, 864)) == 0) {
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

        while (stage_CheckAnimationFrame(PAL_JPNKORCN(868, 864), 390, 0) == 0) {
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

        stage_SetAnimation(PAL_JPNKORCN(869, 865), 1, 0);

        stage_SetAnimation(PAL_JPNKORCN(198, 194), 1, 0);

        scpPlayMot(boyGObj, 473);

        scpPlayMot(scpSearchGobj(2253), 833);

        while (stage_CheckAnimationFrame(PAL_JPNKORCN(869, 865), 390, 0) == 0) {
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

/* The step between the logo and the save. The Japanese and Korean discs (the November
   build) name it actEndLogoFade and have no per-language logo animation before the wait
   (the !ICO_JPNKOR block); the body is one for every disc under the European name. */
static void actEndLogoChk_ntscaug6(GObj *volatile self)
{
    scpBoyControlReadDisable = 1;
    enable_game_pause = 0;
    preload(1);
    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 30);

    scpFadeOut(6.0f, 0, 0, 0);

    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    RequestStageChange(1, boyGObj, 0, 255.0f, 8.0f);
}

void actEndLogoChk(GObj *volatile self)
{
    if (ICO_NTSCAUG6) {
        actEndLogoChk_ntscaug6(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    int id = 60;

    _ACTWait(1);

    scpBoyControlReadDisable = 1;
    enable_game_pause = 0;

    if (!ICO_JPNKORCN) {
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
    }

    SetHandCameraLimitInDemo(0, 0);

    SetZoomMaxValInDemo(0);

    preload(1);

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 30);

    scpFadeOut(6.0f, 0, 0, 0);

    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(PAL_JPNKORCN(id, 60), -1, -2);

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

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6(55, 52, 51));
    scpBoyControlReadDisable = 1;
    scpFadeOut(255.0f, 0, 0, 0);
    if (!ICO_NTSCAUG6) {
        stage_SetAnimation(PAL_JPNKORCN(74, 70), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(75, 71), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(77, 73), 0, -1);
    }
    demo03_mes[0].func = actEndDemo03Chk;
    act->mail = demo03_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
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

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6(55, 52, 51));
    scpBoyControlReadDisable = 1;
    scpFadeOut(255.0f, 0, 0, 0);
    demo04_mes[0].func = actEndDemo04Chk;
    act->mail = demo04_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

void actEndDemo05(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6(55, 52, 51));
    scpBoyControlReadDisable = 1;
    demo05_mes[0].func = actEndDemo05Chk;
    act->mail = demo05_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

void actEndDemo11(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6(55, 52, 51));
    scpBoyControlReadDisable = 1;
    SelectBoyCrown(boyGObj, 2);
    demo11_mes[0].func = actEndDemo11Chk;
    act->mail = demo11_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

void actEndDemo12(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6(55, 52, 51));
    scpBoyControlReadDisable = 1;
    stage_SetAnimation(PAL_JPNKORCN_NTSCAUG6(80, 76, 67), 0, -1);
    demo12_mes[0].func = actEndDemo12Chk;
    act->mail = demo12_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

void actEndDemo13(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6(55, 52, 51));
    scpBoyControlReadDisable = 1;
    scpFadeOut(255.0f, 255, 255, 255);
    SelectBoyCrown(boyGObj, 2);
    demo13_mes[0].func = actEndDemo13Chk;
    act->mail = demo13_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

void actStaff2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6(55, 52, 51));
    scpBoyControlReadDisable = 1;
    if (ICO_NTSC) {
        scpSetBoyWeaponGObj(scpSearchGobj(2591));
        stage_SetAnimation(238, 0, 0);
        stage_SetAnimation(258, 0, 0);
    } else if (ICO_AUG6) {
        stage_SetAnimation(237, 0, 0);
        stage_SetAnimation(257, 0, 0);
    } else {
        scpSetBoyWeaponGObj(scpSearchGobj(2795));
        scpSearchGobj(2793)->dobj->ctrl.seMute = 1;
        scpSearchGobj(2794)->dobj->ctrl.seMute = 1;
        stage_SetAnimation(PAL_JPNKORCN(269, 265), 0, 0);
    }
    staff2_mes[0].func = actStaff2Chk;
    act->mail = staff2_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

void actStaff3(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6(55, 52, 51));
    scpBoyControlReadDisable = 1;
    if (ICO_NTSC) {
        DeleteBoyWeapon();
        stage_SetAnimation(59, 0, 0);
        stage_SetAnimation(64, 0, 0);
    } else if (ICO_AUG6) {
        stage_SetAnimation(59, 0, 0);
        stage_SetAnimation(64, 0, 0);
    } else {
        scpFadeOut(255.0f, 0, 0, 0);
        DeleteBoyWeapon();
        stage_SetAnimation(PAL_JPNKORCN(72, 68), 0, 0);
        stage_SetAnimation(PAL_JPNKORCN(77, 73), 0, 0);
    }
    staff3_mes[0].func = actStaff3Chk;
    act->mail = staff3_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

void actEndLogo(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);

    _ACTWait(1);

    logo_mes[0].func = actEndLogoChk;
    act->mail = logo_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
    _ACTWait(0);
}

void actEndDemo01Chk(GObj *volatile self)
{
    scpAdpcmPlayRequestFunc(PAL_NTSCAUG6(43, 42), &ed1, 0, 1, 1);

    while (ed1 == 0) {
        _ACTWait(1);
    }

    preload(2);

    scpFadeIn(6.0f);

    gflagOn(2);

    gflagOn(3);

    gflagOn(4);

    gflagOn(PAL_NTSC_AUG6(339, 308, 307));

    actCreateSubThread(actConte14_1, 21);
}

void actEndDemo02Chk(GObj *volatile self)
{
    scpAdpcmPlayRequestFunc(PAL_NTSCAUG6(44, 43), &ed2, 0, 1, 1);

    while (ed2 == 0) {
        _ACTWait(1);
    }

    preload(3);

    scpFadeIn(6.0f);

    gflagOn(PAL_NTSC_AUG6(340, 309, 308));

    actCreateSubThread(actConte14_2, 21);
}

void actEndDemo03Chk(GObj *volatile self)
{
    scpAdpcmPlayRequestFunc(PAL_NTSCAUG6(45, 44), &ed3, 0, 1, 1);

    while (ed3 == 0) {
        _ACTWait(1);
    }

    preload(2);

    scpFadeIn(6.0f);

    gflagOn(PAL_NTSC_AUG6(341, 310, 309));

    actCreateSubThread(actConte14_3, 21);
}

void actConte14_3(GObj *volatile self)
{
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(833, 829, 759, 758), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6(833, 829, 759, 758)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    RequestStageChange(2, boyGObj, 0, 1.0f, 8.0f);
}

void actEndDemo04Chk(GObj *volatile self)
{
    scpAdpcmPlayRequestFunc(PAL_NTSCAUG6(46, 45), &ed4, 0, 1, 1);

    while (ed4 == 0) {
        _ACTWait(1);
    }

    preload(5);

    scpFadeIn(6.0f);

    gflagOn(PAL_NTSC_AUG6(342, 311, 310));

    actCreateSubThread(actConte14_4, 21);
}

void actConte14_4(GObj *volatile self)
{
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(834, 830, 760, 759), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6(834, 830, 760, 759)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    RequestStageChange(5, boyGObj, girlGObj, 1.0f, 8.0f);
}

void actEndDemo05Chk(GObj *volatile self)
{
    gflagOn(PAL_NTSC_AUG6(343, 312, 311));

    actCreateSubThread(actConte14_5, 21);
}

void actConte14_5(GObj *volatile self)
{
    preload(6);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(835, 831, 761, 760), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6(835, 831, 761, 760)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    RequestStageChange(6, boyGObj, girlGObj, 1.0f, 8.0f);
}

void actEndDemo06Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        gflagOff(308);
    } else if (ICO_AUG6) {
        gflagOff(307);
    } else {
        scpAdpcmPlayRequestFunc(47, &sea, 0, 1, 1);
    }

    if (ICO_NTSC) {
        gflagOff(312);
    } else if (ICO_AUG6) {
        gflagOff(311);
    } else {
        while (sea == 0) {
            _ACTWait(1);
        }

        scpFadeIn(6.0f);

        gflagOff(339);

        gflagOff(343);
    }

    actCreateSubThread(actConte14_6, 21);
}

void actEndDemo07Chk(GObj *volatile self)
{
    if (ICO_NTSCAUG6) {
        scpAdpcmPlayRequestFunc(46, &ed5, 0, 1, 1);

        while (ed5 == 0) {
            _ACTWait(1);
        }
    } else {
        _ACTWait(30);
    }

    scpFadeIn(6.0f);

    gflagOn(PAL_NTSC_AUG6(345, 314, 313));

    actCreateSubThread(actConte14_7, 21);
}

void actEndDemo10Chk(GObj *volatile self)
{
    scpAdpcmPlayRequestFunc(PAL_NTSCAUG6(48, 47), &ed6, 0, 1, PAL_NTSCAUG6(0, 1));

    while (ed6 == 0) {
        _ACTWait(1);
    }

    scpFadeIn(6.0f);

    gflagOn(PAL_NTSC_AUG6(348, 317, 316));

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
    gflagOn(PAL_NTSC_AUG6(349, 318, 317));

    actCreateSubThread(actConte14_11, 21);
}

void actConte14_11(GObj *volatile self)
{
    preload(2);

    scpPlayStart(boyGObj);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(852, 848, 778, 777), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6(852, 848, 778, 777)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    RequestStageChange(2, boyGObj, girlGObj, 1.0f, 8.0f);
}

void actEndDemo12Chk(GObj *volatile self)
{
    gflagOn(PAL_NTSC_AUG6(350, 319, 318));

    actCreateSubThread(actConte14_12, 21);
}

void actConte14_12(GObj *volatile self)
{
    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6(853, 849, 779, 778), 1, 0);

    preload(4);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6(853, 849, 779, 778)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    RequestStageChangeWithColor(4, boyGObj, girlGObj, 16.0f, 16.0f, 255, 255, 255);
}

void actEndDemo13Chk(GObj *volatile self)
{
    gflagOn(PAL_NTSC_AUG6(351, 320, 319));

    actCreateSubThread(actConte14_13, 21);
}

void actStaff1Chk(GObj *volatile self)
{
    /* PC port: the Extras credits start here, with the
       ending's song where the real ending has it when the roll starts (it
       began in actEndDemo06Chk, several scenes earlier) */
    if (ico_credits_active()) {
        scpAdpcmPlayRequestFunc(ico_credits_song_start(), &sea, 0, 1, 1);
    }
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
    if (!ICO_NTSCAUG6) {
        scpAdpcmPlayRequestFunc(49, &staff3, 0, 1, 1);

        while (staff3 == 0) {
            _ACTWait(1);
        }

        scpFadeIn(6.0f);
    }

    actCreateSubThread(actStaff3Demo, 21);

    actCreateSubThread(actStaff3RollChk, 21);
}

void actStaff3RollChk(GObj *volatile self)
{
    preload(1);

    while (staffRollStartFlag != 0) {
        _ACTWait(1);
    }

    /* PC port: the Extras credits end at the title, with
       every stream faded as this fades ed6, and no beach, logo or save */
    if (ico_credits_active()) {
        AdpcmFadeCloseAll(80);
        RequestStageChangeSimple(ICO_CREDITS_TITLE_STAGE, 16.0f, 8.0f, 0, 0, 0);
        return;
    }

    if (ed6 != 0) {
        if (ICO_NTSCAUG6) {
            scpAdpcmFadeClose(ed6, 80);
        } else {
            scpAdpcmFadeCloseFunc(&ed6, 80);
        }
    }

    RequestStageChange(1, boyGObj, 0, 16.0f, 0.001f);
}

void actEndDemo14Chk(GObj *volatile self)
{
    gflagOn(PAL_NTSC_AUG6(352, 321, 320));

    actCreateSubThread(actConte14_14, 21);
}

/* The ending's save step, one body for every disc under the European name. The
   Japanese and Korean discs (the November build) still call it actEndLogoChk, its name in
   the Aug-6 disc's TRFILE (the last function of end.c there too); January renamed it
   actEndingSave and gave the name to the step before it. */
static void actEndingSave(GObj *volatile self)
{
    /* PC port: the Extras credits never get here (they end
       at actStaff3RollChk); if they did, no ending signal and no save */
    if (ico_credits_active()) {
        RequestStageChange(1, boyGObj, 0, 255.0f, 8.0f);
        return;
    }
    ico_gs_signal(ICO_GS_EV_ENDING, gFlagGameClear);
    if (gFlagGameClear == 0) {
        int save;

        gFlagGameClear = 1;
        save = IosMcPreviewInfo[2];
        gflagInit();
        IosMcPreviewInfo[2] = save;
        gflagOn(395);
        lt_switch_layout(PAL_JPNKORCN(28, 25));
        _ACTWait(60);

        while (current_layout_id != PAL_JPNKORCN(54, 51)) {
            _ACTWait(1);
        }
        gFlagGameClear = 0;
    }

    scpFadeOut(PAL_JPNKORCN(255.0f, 6.0f), 0, 0, 0);

    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    optionScreenMode = 0;
    RequestStageChange(1, boyGObj, 0, 255.0f, 8.0f);
}
