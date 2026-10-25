#include "st04l.h"
#include "layout_texture.h"
#include "pad.h"
#include "thread.h"
#include "obj_manager.h"
#include "adpcm_init.h"
#include "s_init.h"
#include "act-game.h"
#include "act.h"
#include "gobj_process.h"
#include "boyact.h"
#include "commonact.h"
#include "girl_act.h"
#include "way_llf.h"
#include "camera-root.h"
#include "fightSound.h"
#include "generator.h"
#include "lws_kyomi.h"
#include "gflag.h"
#include "st04r.h"
#include "StageAnimation.h"
#include "attackCheckBoundary.h"
#include "motionManager2.h"
#include "weapon.h"
#include <libvu0.h>
#include "typedef.h"
#include "st04r.h"
#include "script.h"
#include "main.h"

static void actSt04eSolarBeamChkSub(GObj *volatile self);

/* .sbss: the four arguments turnBall hands to the ball-turn actor through file
   scope, the demo's own end flag, the crest-2 animation the room selects, and
   the flag the stair subthread raises once it is past its setup. */
static int turnFlag; /* derived name */

static int turnAnim; /* derived name */

static int turnGobj1; /* derived name */

static int turnGobj2; /* derived name */

static int demoEnd;

static int crest2Anim; /* derived name */

static int subStarted; /* derived name */

/* A 16-byte constant vector template: the float view carries the values,
   the long long view is the one the copy reads. */

static const ConstVec stairSubPos = {{0.0f, 0.0f, -5000.0f, 1.0f}}; /* derived name */

/* .sdata: the room's stream handles and shakes (solar4l unused). */
SqEntry *ball1_4l = 0;

SqEntry *ball2_4l = 0;

SqEntry *ball3_4l = 0;

SqEntry *crest1 = 0;

SqEntry *crest2 = 0;

SqEntry *crest3 = 0;

SqEntry *solar4l = 0;

SqEntry *stair4d = 0;

SqEntry *st04d_hasi = 0;

SqEntry *sekizo4c = 0;

unsigned int oriup4c = 0;

/* the 2001 builds' stream handle under this name (the European program keeps a
   different value in it) */
SqEntry *oriup4c_ntscaug6jun26 = 0;

unsigned char oridown4c = 0;

/* the 2001 builds' stream handle under this name (the European program keeps a
   different value in it) */
SqEntry *oridown4c_ntscaug6jun26 = 0;

unsigned int st04l_yure = 0;

unsigned char st04l_yure_vol = 0;

unsigned int sekizo_4l = 0;

unsigned char sekizo_4l_vol = 0;

void actSt04cInit(void)
{
    if (gflagChk(PAL_NTSCAUG6JUN26(212, 192)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(234, 230, 206, 205), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(234, 230, 206, 205), 0, -1);
    }

    if (gflagChk(PAL_NTSCAUG6JUN26(213, 193)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(235, 231, 207, 206), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(235, 231, 207, 206), 0, -1);
    }

    if (gflagChk(PAL_NTSCAUG6JUN26(214, 194)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(236, 232, 208, 207), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(236, 232, 208, 207), 0, -1);
    }

    if (gflagChk(PAL_NTSCAUG6JUN26(215, 195)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(237, 233, 209, 208), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(237, 233, 209, 208), 0, -1);
    }

    if (gflagChk(PAL_NTSCAUG6JUN26(216, 196)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(238, 234, 210, 209), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(238, 234, 210, 209), 0, -1);
    }

    if (gflagChk(PAL_NTSCAUG6JUN26(217, 197)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(239, 235, 211, 210), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(239, 235, 211, 210), 0, -1);
    }

    if (gflagChk(PAL_NTSCAUG6JUN26(218, 198)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(240, 236, 212, 211), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(240, 236, 212, 211), 0, -1);
    }

    if (gflagChk(PAL_NTSCAUG6JUN26(188, 169)) == 0) {
        SetWayGroupActive(1, 0);
    } else {
        SetWayGroupActive(1, 1);
    }

    if (gflagChk(PAL_NTSCAUG6JUN26(182, 163)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(224, 220, 196, 195), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(224, 220, 196, 195), 0, -1);
    }

    if (gflagChk(PAL_NTSCAUG6JUN26(183, 164)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(225, 221, 197, 196), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(225, 221, 197, 196), 0, -1);
    }
}

static void actSt04dInit_ntsc(void)
{
    if (gflagChk(161) == 0) {
        stage_SetAnimation(199, 0, 0);
        stage_SetAnimation(230, 0, 0);
    } else {
        stage_SetAnimation(199, 0, -1);
        stage_SetAnimation(230, 0, -1);
    }

    if (gflagChk(163) == 0) {
        stage_SetAnimation(196, 0, 0);
        SetWayGroupActive(3, 0);
    } else {
        stage_SetAnimation(196, 0, -1);
        SetWayGroupActive(3, 1);
    }

    if (gflagChk(164) == 0) {
        stage_SetAnimation(197, 0, 0);
        SetWayGroupActive(4, 0);
    } else {
        stage_SetAnimation(197, 0, -1);
        SetWayGroupActive(4, 1);
    }

    if (gflagChk(192) == 0) {
        stage_SetAnimation(206, 0, 0);
    } else {
        stage_SetAnimation(206, 0, -1);
    }

    if (gflagChk(193) == 0) {
        stage_SetAnimation(207, 0, 0);
    } else {
        stage_SetAnimation(207, 0, -1);
    }

    if (gflagChk(194) == 0) {
        stage_SetAnimation(208, 0, 0);
    } else {
        stage_SetAnimation(208, 0, -1);
    }

    if (gflagChk(195) == 0) {
        stage_SetAnimation(209, 0, 0);
    } else {
        stage_SetAnimation(209, 0, -1);
    }

    if (gflagChk(196) == 0) {
        stage_SetAnimation(210, 0, 0);
    } else {
        stage_SetAnimation(210, 0, -1);
    }

    if (gflagChk(197) == 0) {
        stage_SetAnimation(211, 0, 0);
    } else {
        stage_SetAnimation(211, 0, -1);
    }

    if (gflagChk(198) == 0) {
        stage_SetAnimation(212, 0, 0);
    } else {
        stage_SetAnimation(212, 0, -1);
    }

    if (gflagChk(145) != 0 && gflagChk(155) == 0) {
        stage_SetAnimation(225, 0, -1);
    }

    if (gflagChk(145) == 0 || gflagChk(155) != 0) {
        stage_SetAnimation(225, 0, 0);
    }

    if (gflagChk(171) != 0) {
        stage_SetAnimation(200, 0, 200);
    } else {
        stage_SetAnimation(200, 0, 0);
    }
}

static void actSt04dInit_aug6jun26(void)
{
    if (gflagChk(161) == 0) {
        stage_SetAnimation(198, 0, 0);
        stage_SetAnimation(229, 0, 0);
    } else {
        stage_SetAnimation(198, 0, -1);
        stage_SetAnimation(229, 0, -1);
    }

    if (gflagChk(163) == 0) {
        stage_SetAnimation(195, 0, 0);
        SetWayGroupActive(3, 0);
    } else {
        stage_SetAnimation(195, 0, -1);
        SetWayGroupActive(3, 1);
    }

    if (gflagChk(164) == 0) {
        stage_SetAnimation(196, 0, 0);
        SetWayGroupActive(4, 0);
    } else {
        stage_SetAnimation(196, 0, -1);
        SetWayGroupActive(4, 1);
    }

    if (gflagChk(192) == 0) {
        stage_SetAnimation(205, 0, 0);
    } else {
        stage_SetAnimation(205, 0, -1);
    }

    if (gflagChk(193) == 0) {
        stage_SetAnimation(206, 0, 0);
    } else {
        stage_SetAnimation(206, 0, -1);
    }

    if (gflagChk(194) == 0) {
        stage_SetAnimation(207, 0, 0);
    } else {
        stage_SetAnimation(207, 0, -1);
    }

    if (gflagChk(195) == 0) {
        stage_SetAnimation(208, 0, 0);
    } else {
        stage_SetAnimation(208, 0, -1);
    }

    if (gflagChk(196) == 0) {
        stage_SetAnimation(209, 0, 0);
    } else {
        stage_SetAnimation(209, 0, -1);
    }

    if (gflagChk(197) == 0) {
        stage_SetAnimation(210, 0, 0);
    } else {
        stage_SetAnimation(210, 0, -1);
    }

    if (gflagChk(198) == 0) {
        stage_SetAnimation(211, 0, 0);
    } else {
        stage_SetAnimation(211, 0, -1);
    }

    if (gflagChk(145) != 0 && gflagChk(155) == 0) {
        stage_SetAnimation(224, 0, -1);
    }

    if (gflagChk(145) == 0 || gflagChk(155) != 0) {
        stage_SetAnimation(224, 0, 0);
    }

    if (gflagChk(171) != 0) {
        stage_SetAnimation(199, 0, 200);
    } else {
        stage_SetAnimation(199, 0, 0);
    }
}

void actSt04dInit(void)
{
    if (ICO_NTSC) {
        actSt04dInit_ntsc();
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04dInit_aug6jun26();
        return;
    }
    if (gflagChk(180) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(227, 223), 0, 0);
        stage_SetAnimation(PAL_JPNKORCN(258, 254), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(227, 223), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(258, 254), 0, -1);
        FinishHint(17);
    }

    if (gflagChk(182) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(224, 220), 0, 0);
        SetWayGroupActive(3, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(224, 220), 0, -1);
        SetWayGroupActive(3, 1);
    }

    if (gflagChk(183) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(225, 221), 0, 0);
        SetWayGroupActive(4, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(225, 221), 0, -1);
        SetWayGroupActive(4, 1);
    }

    if (gflagChk(212) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(234, 230), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(234, 230), 0, -1);
    }

    if (gflagChk(213) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(235, 231), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(235, 231), 0, -1);
    }

    if (gflagChk(214) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(236, 232), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(236, 232), 0, -1);
    }

    if (gflagChk(215) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(237, 233), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(237, 233), 0, -1);
    }

    if (gflagChk(216) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(238, 234), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(238, 234), 0, -1);
    }

    if (gflagChk(217) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(239, 235), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(239, 235), 0, -1);
    }

    if (gflagChk(218) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(240, 236), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(240, 236), 0, -1);
    }

    if (gflagChk(162) != 0 && gflagChk(174) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(253, 249), 0, -1);
    }

    if (gflagChk(162) == 0 || gflagChk(174) != 0) {
        stage_SetAnimation(PAL_JPNKORCN(253, 249), 0, 0);
    }

    if (gflagChk(190) != 0) {
        stage_SetAnimation(PAL_JPNKORCN(228, 224), 0, 200);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(228, 224), 0, 0);
    }
}

void actSt04eInit(void)
{
    if (gflagChk(PAL_NTSCAUG6JUN26(230, 205)) == 0) {
        SetWayGroupActive(5, 0);
    } else {
        SetWayGroupActive(5, 1);
    }

    if (gflagChk(PAL_NTSCAUG6JUN26(182, 163)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(224, 220, 196, 195), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(224, 220, 196, 195), 0, -1);
    }

    if (gflagChk(PAL_NTSCAUG6JUN26(183, 164)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(225, 221, 197, 196), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(225, 221, 197, 196), 0, -1);
    }
}

static void actSt04lBallTurnCommonSub(GObj *volatile self)
{
    _ACTWait(60);

    scpAdpcmPlayRequestFunc(82, &ball1_4l, 1, 1, 1);

    while (ball1_4l == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(turnAnim, 1, 0);

    while (stage_CheckAnimationFinish(turnAnim) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 6);

    demoEnd = 1;
    _ACTWait(0);
}

static void actSt04lBallTurnCommon(GObj *volatile self)
{
    GProc *h;

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    gflagOn(turnFlag);
    scpSleepEnemyAll();

    h = actCreateSubThread(actSt04lBallTurnCommonSub, 21);

    demoEnd = 0;
    ball1_4l = (SqEntry *)ICO_INVALID_PTR;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&h->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        while (ball1_4l == 0) {
            _ACTWait(1);
        }

        if (ball1_4l != (SqEntry *)ICO_INVALID_PTR) {
            scpAdpcmFadeCloseFunc(&ball1_4l, 512);
        }

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(turnAnim, 0, -1);
        scpFadeIn(5.0f);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
    }

    scpSearchGobj(turnGobj1)->active = 1;
    scpSearchGobj(turnGobj2)->active = 1;
    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

static ActMail c1BallMain_mes_pal[2] = {{406, actSt04lC1BallSwitch}, {429}}; /* derived name */

static ActMail c1BallMain_mes_ntsc[2] = {{376, actSt04lC1BallSwitch}, {396}}; /* derived name */

static ActMail c1BallMain_mes_aug6jun26[2] = {{372, actSt04lC1BallSwitch},
                                              {392}}; /* derived name */

#define c1BallMain_mes                                                                             \
    PAL_NTSC_AUG6JUN26(c1BallMain_mes_pal, c1BallMain_mes_ntsc, c1BallMain_mes_aug6jun26)

static ActMail c1Ball_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail c1Ball_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail c1Ball_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define c1Ball_mes PAL_NTSC_AUG6JUN26(c1Ball_mes_pal, c1Ball_mes_ntsc, c1Ball_mes_aug6jun26)

static ActMail c1BallSwitch_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail c1BallSwitch_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail c1BallSwitch_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define c1BallSwitch_mes                                                                           \
    PAL_NTSC_AUG6JUN26(c1BallSwitch_mes_pal, c1BallSwitch_mes_ntsc, c1BallSwitch_mes_aug6jun26)

static ActMail c1BallTurn_mes[2] = {{430}, {429}}; /* derived name */

static ActMail turnBall_mes[2] = {{430}, {429}}; /* derived name */

static ActMail c2BallMain_mes_pal[2] = {{406, actSt04lC2BallSwitch}, {429}}; /* derived name */

static ActMail c2BallMain_mes_ntsc[2] = {{376, actSt04lC2BallSwitch}, {396}}; /* derived name */

static ActMail c2BallMain_mes_aug6jun26[2] = {{372, actSt04lC2BallSwitch},
                                              {392}}; /* derived name */

#define c2BallMain_mes                                                                             \
    PAL_NTSC_AUG6JUN26(c2BallMain_mes_pal, c2BallMain_mes_ntsc, c2BallMain_mes_aug6jun26)

static ActMail c2Ball_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail c2Ball_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail c2Ball_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define c2Ball_mes PAL_NTSC_AUG6JUN26(c2Ball_mes_pal, c2Ball_mes_ntsc, c2Ball_mes_aug6jun26)

static ActMail c2BallSwitch_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail c2BallSwitch_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail c2BallSwitch_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define c2BallSwitch_mes                                                                           \
    PAL_NTSC_AUG6JUN26(c2BallSwitch_mes_pal, c2BallSwitch_mes_ntsc, c2BallSwitch_mes_aug6jun26)

static ActMail c2BallTurn_mes[2] = {{430}, {429}}; /* derived name */

static ActMail c3BallMain_mes_pal[2] = {{407, actSt04lC3BallSwitch}, {429}}; /* derived name */

static ActMail c3BallMain_mes_ntsc[2] = {{377, actSt04lC3BallSwitch}, {396}}; /* derived name */

static ActMail c3BallMain_mes_aug6jun26[2] = {{373, actSt04lC3BallSwitch},
                                              {392}}; /* derived name */

#define c3BallMain_mes                                                                             \
    PAL_NTSC_AUG6JUN26(c3BallMain_mes_pal, c3BallMain_mes_ntsc, c3BallMain_mes_aug6jun26)

static ActMail c3Ball_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail c3Ball_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail c3Ball_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define c3Ball_mes PAL_NTSC_AUG6JUN26(c3Ball_mes_pal, c3Ball_mes_ntsc, c3Ball_mes_aug6jun26)

static ActMail c3BallSwitch_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail c3BallSwitch_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail c3BallSwitch_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define c3BallSwitch_mes                                                                           \
    PAL_NTSC_AUG6JUN26(c3BallSwitch_mes_pal, c3BallSwitch_mes_ntsc, c3BallSwitch_mes_aug6jun26)

static ActMail c3BallTurn_mes[2] = {{430}, {429}}; /* derived name */

static ActMail crest01_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail crest01_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail crest01_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define crest01_mes PAL_NTSC_AUG6JUN26(crest01_mes_pal, crest01_mes_ntsc, crest01_mes_aug6jun26)

static ActMail crest02_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail crest02_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail crest02_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define crest02_mes PAL_NTSC_AUG6JUN26(crest02_mes_pal, crest02_mes_ntsc, crest02_mes_aug6jun26)

static ActMail crest03_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail crest03_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail crest03_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define crest03_mes PAL_NTSC_AUG6JUN26(crest03_mes_pal, crest03_mes_ntsc, crest03_mes_aug6jun26)

static ActMail st04eSolarBeam_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail st04eSolarBeam_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail st04eSolarBeam_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define st04eSolarBeam_mes                                                                         \
    PAL_NTSC_AUG6JUN26(st04eSolarBeam_mes_pal, st04eSolarBeam_mes_ntsc,                            \
                       st04eSolarBeam_mes_aug6jun26)

static ActMail stair_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail stair_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail stair_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define stair_mes PAL_NTSC_AUG6JUN26(stair_mes_pal, stair_mes_ntsc, stair_mes_aug6jun26)

static ActMail rope1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail rope1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail rope1_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define rope1_mes PAL_NTSC_AUG6JUN26(rope1_mes_pal, rope1_mes_ntsc, rope1_mes_aug6jun26)

static ActMail rope2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail rope2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail rope2_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define rope2_mes PAL_NTSC_AUG6JUN26(rope2_mes_pal, rope2_mes_ntsc, rope2_mes_aug6jun26)

static ActMail brg1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail brg1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail brg1_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define brg1_mes PAL_NTSC_AUG6JUN26(brg1_mes_pal, brg1_mes_ntsc, brg1_mes_aug6jun26)

static ActMail brg1Chk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail rope3_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail rope3_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail rope3_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define rope3_mes PAL_NTSC_AUG6JUN26(rope3_mes_pal, rope3_mes_ntsc, rope3_mes_aug6jun26)

static ActMail rope4_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail rope4_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail rope4_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define rope4_mes PAL_NTSC_AUG6JUN26(rope4_mes_pal, rope4_mes_ntsc, rope4_mes_aug6jun26)

static ActMail brg2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail brg2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail brg2_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define brg2_mes PAL_NTSC_AUG6JUN26(brg2_mes_pal, brg2_mes_ntsc, brg2_mes_aug6jun26)

static ActMail brg2Chk_mes[2] = {{430}, {429}}; /* derived name */

static ActMail brg1Way_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail brg1Way_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail brg1Way_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define brg1Way_mes PAL_NTSC_AUG6JUN26(brg1Way_mes_pal, brg1Way_mes_ntsc, brg1Way_mes_aug6jun26)

static ActMail brg2Way_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail brg2Way_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail brg2Way_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define brg2Way_mes PAL_NTSC_AUG6JUN26(brg2Way_mes_pal, brg2Way_mes_ntsc, brg2Way_mes_aug6jun26)

static ActMail sekizo_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail sekizo_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail sekizo_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define sekizo_mes PAL_NTSC_AUG6JUN26(sekizo_mes_pal, sekizo_mes_ntsc, sekizo_mes_aug6jun26)

static ActMail turi_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail turi_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail turi_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define turi_mes PAL_NTSC_AUG6JUN26(turi_mes_pal, turi_mes_ntsc, turi_mes_aug6jun26)

static ActMail gondola_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gondola_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gondola_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define gondola_mes PAL_NTSC_AUG6JUN26(gondola_mes_pal, gondola_mes_ntsc, gondola_mes_aug6jun26)

static ActMail gondola2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gondola2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gondola2_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define gondola2_mes PAL_NTSC_AUG6JUN26(gondola2_mes_pal, gondola2_mes_ntsc, gondola2_mes_aug6jun26)

static ActMail gondolaChk_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gondolaChk_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gondolaChk_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define gondolaChk_mes                                                                             \
    PAL_NTSC_AUG6JUN26(gondolaChk_mes_pal, gondolaChk_mes_ntsc, gondolaChk_mes_aug6jun26)

static ActMail gondola_chara_pal[2] = {{430}, {429}}; /* derived name */

static ActMail gondola_chara_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail gondola_chara_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define gondola_chara                                                                              \
    PAL_NTSC_AUG6JUN26(gondola_chara_pal, gondola_chara_ntsc, gondola_chara_aug6jun26)

static ActMail monyou01_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail monyou01_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail monyou01_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define monyou01_mes PAL_NTSC_AUG6JUN26(monyou01_mes_pal, monyou01_mes_ntsc, monyou01_mes_aug6jun26)

static ActMail monyou02_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail monyou02_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail monyou02_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define monyou02_mes PAL_NTSC_AUG6JUN26(monyou02_mes_pal, monyou02_mes_ntsc, monyou02_mes_aug6jun26)

static ActMail monyou03_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail monyou03_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail monyou03_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define monyou03_mes PAL_NTSC_AUG6JUN26(monyou03_mes_pal, monyou03_mes_ntsc, monyou03_mes_aug6jun26)

static ActMail monyou04_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail monyou04_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail monyou04_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define monyou04_mes PAL_NTSC_AUG6JUN26(monyou04_mes_pal, monyou04_mes_ntsc, monyou04_mes_aug6jun26)

static ActMail monyou05_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail monyou05_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail monyou05_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define monyou05_mes PAL_NTSC_AUG6JUN26(monyou05_mes_pal, monyou05_mes_ntsc, monyou05_mes_aug6jun26)

static ActMail monyou06_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail monyou06_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail monyou06_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define monyou06_mes PAL_NTSC_AUG6JUN26(monyou06_mes_pal, monyou06_mes_ntsc, monyou06_mes_aug6jun26)

static ActMail monyou07_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail monyou07_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail monyou07_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define monyou07_mes PAL_NTSC_AUG6JUN26(monyou07_mes_pal, monyou07_mes_ntsc, monyou07_mes_aug6jun26)

static ActMail ori_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail ori_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail ori_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define ori_mes PAL_NTSC_AUG6JUN26(ori_mes_pal, ori_mes_ntsc, ori_mes_aug6jun26)

static ActMail oriRopeCutR_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail oriRopeCutR_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail oriRopeCutR_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define oriRopeCutR_mes                                                                            \
    PAL_NTSC_AUG6JUN26(oriRopeCutR_mes_pal, oriRopeCutR_mes_ntsc, oriRopeCutR_mes_aug6jun26)

static ActMail oriRopeCutL_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail oriRopeCutL_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail oriRopeCutL_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define oriRopeCutL_mes                                                                            \
    PAL_NTSC_AUG6JUN26(oriRopeCutL_mes_pal, oriRopeCutL_mes_ntsc, oriRopeCutL_mes_aug6jun26)

static ActMail ori2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail ori2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail ori2_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define ori2_mes PAL_NTSC_AUG6JUN26(ori2_mes_pal, ori2_mes_ntsc, ori2_mes_aug6jun26)

static ActMail sword_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail sword_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail sword_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define sword_mes PAL_NTSC_AUG6JUN26(sword_mes_pal, sword_mes_ntsc, sword_mes_aug6jun26)

static ActMail torch1_1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail torch1_1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail torch1_1_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define torch1_1_mes PAL_NTSC_AUG6JUN26(torch1_1_mes_pal, torch1_1_mes_ntsc, torch1_1_mes_aug6jun26)

static ActMail torch1_2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail torch1_2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail torch1_2_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define torch1_2_mes PAL_NTSC_AUG6JUN26(torch1_2_mes_pal, torch1_2_mes_ntsc, torch1_2_mes_aug6jun26)

static ActMail torch2_1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail torch2_1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail torch2_1_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define torch2_1_mes PAL_NTSC_AUG6JUN26(torch2_1_mes_pal, torch2_1_mes_ntsc, torch2_1_mes_aug6jun26)

static ActMail torch2_2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail torch2_2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail torch2_2_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define torch2_2_mes PAL_NTSC_AUG6JUN26(torch2_2_mes_pal, torch2_2_mes_ntsc, torch2_2_mes_aug6jun26)

static ActMail torch3_1_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail torch3_1_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail torch3_1_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define torch3_1_mes PAL_NTSC_AUG6JUN26(torch3_1_mes_pal, torch3_1_mes_ntsc, torch3_1_mes_aug6jun26)

static ActMail torch3_2_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail torch3_2_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail torch3_2_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define torch3_2_mes PAL_NTSC_AUG6JUN26(torch3_2_mes_pal, torch3_2_mes_ntsc, torch3_2_mes_aug6jun26)

static ActMail torch2_1XL_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail torch2_1XL_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail torch2_1XL_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define torch2_1XL_mes                                                                             \
    PAL_NTSC_AUG6JUN26(torch2_1XL_mes_pal, torch2_1XL_mes_ntsc, torch2_1XL_mes_aug6jun26)

static ActMail torch2_2XL_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail torch2_2XL_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail torch2_2XL_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define torch2_2XL_mes                                                                             \
    PAL_NTSC_AUG6JUN26(torch2_2XL_mes_pal, torch2_2XL_mes_ntsc, torch2_2XL_mes_aug6jun26)

void turnBall(GObj *self, int flag, int anim, int gobj1, int gobj2)
{
    Act *sub = GOBJ_ACT(self);

    turnFlag = flag;
    turnAnim = anim;
    turnGobj1 = gobj1;
    turnGobj2 = gobj2;

    turnBall_mes[0].func = actSt04lBallTurnCommon;
    sub->mail = turnBall_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04lCrest02(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(175, 156)) == 0) {
        if (current_stage_no == 19) {
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(201, 197, 179, 178), 0, 0);
        } else {
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(202, 198, 180, 179), 0, 0);
        }

        crest02_mes[0].func = actSt04lCrest2Main;
        act->mail = crest02_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        if (current_stage_no == 19) {
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(201, 197, 179, 178), 0, -1);
        } else {
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(202, 198, 180, 179), 0, -1);
        }
    }
}

static void actSt04lCrestSub(GObj *volatile self)
{
    stage_SetAnimation(PAL_JPNKORCN(252, 248), 1, 0);
    stage_SetAnimation(PAL_JPNKORCN(199, 195), 1, 0);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(199, 195), 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    oriup4c = iosPadActRequest(boyPad, 10);
    oridown4c = 128;
    iosPadActVolumeSet(oriup4c, 128);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(199, 195), 190, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActStop(oriup4c);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(199, 195)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

static void actSt04lCrestMain_ntsc(GObj *volatile self)
{
    while (scpIsTorchLightOn(1026) == 0 || scpIsTorchLightOn(1027) == 0 || gflagChk(158) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    fightSoundProcessRequestPause();

    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    scpAdpcmPlayRequestFunc(26, &crest1, 0, 1, 1);

    while (crest1 == 0) {
        _ACTWait(1);
    }

    preload(3);
    gflagOn(155);
    gflagOn(204);

    stage_SetAnimation(224, 1, 0);
    stage_SetAnimation(177, 1, 0);

    while (stage_CheckAnimationFrame(177, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    st04l_yure = iosPadActRequest(boyPad, 10);
    st04l_yure_vol = 128;
    iosPadActVolumeSet(st04l_yure, 128);

    while (stage_CheckAnimationFrame(177, 190, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActStop(st04l_yure);

    while (stage_CheckAnimationFinish(177) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    scpWakeupEnemyAll();
    RequestStageChange(3, boyGObj, 0, 2.0f, 8.0f);
}

static void actSt04lCrestMain_aug6jun26(GObj *volatile self)
{
    while (scpIsTorchLightOn(1023) == 0 || scpIsTorchLightOn(1024) == 0 || gflagChk(158) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    fightSoundProcessRequestPause();

    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    scpAdpcmPlayRequestFunc(26, &crest1, 0, 1, 1);

    while (crest1 == 0) {
        _ACTWait(1);
    }

    preload(3);
    gflagOn(155);
    gflagOn(204);

    stage_SetAnimation(223, 1, 0);
    stage_SetAnimation(176, 1, 0);

    while (stage_CheckAnimationFrame(176, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    st04l_yure = iosPadActRequest(boyPad, 10);
    st04l_yure_vol = 128;
    iosPadActVolumeSet(st04l_yure, 128);

    while (stage_CheckAnimationFrame(176, 190, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActStop(st04l_yure);

    while (stage_CheckAnimationFinish(176) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    scpWakeupEnemyAll();
    RequestStageChange(3, boyGObj, girlGObj, 2.0f, 8.0f);
}

void actSt04lCrestMain(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lCrestMain_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lCrestMain_aug6jun26(self);
        return;
    }
    GProc *h;

    while (scpIsTorchLightOn(1114) == 0 || scpIsTorchLightOn(1115) == 0 || gflagChk(177) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    fightSoundProcessRequestPause();

    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }

    scpAdpcmPlayRequestFunc(27, &ball2_4l, 0, 1, 1);

    while (ball2_4l == 0) {
        _ACTWait(1);
    }

    preload(3);
    gflagOn(174);
    gflagOn(224);

    h = actCreateSubThread(actSt04lCrestSub, 21);

    demoEnd = 0;
    oriup4c = 0xFFFFFFFF;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&h->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        scpAdpcmFadeCloseFunc(&ball2_4l, 256);
        iosPadActStop(oriup4c);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(PAL_JPNKORCN(252, 248), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(199, 195), 0, -1);
        scpFadeIn(3.0f);
    }

    scpWakeupEnemyAll();
    RequestStageChange(3, boyGObj, 0, 2.0f, 8.0f);
}

static void actSt04lCrest2Sub(GObj *volatile self)
{
    stage_SetAnimation(crest2Anim, 1, 0);

    while (stage_CheckAnimationFrame(crest2Anim, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    oriup4c = iosPadActRequest(boyPad, 10);
    oridown4c = 128;
    iosPadActVolumeSet(oriup4c, 128);

    while (stage_CheckAnimationFrame(crest2Anim, 190, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActStop(oriup4c);
    oriup4c = 0xFFFFFFFF;

    while (stage_CheckAnimationFinish(crest2Anim) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    if (current_stage_no == 19) {
        scpSearchGobj(1122)->active = 1;
    }

    demoEnd = 1;
    _ACTWait(0);
}

static void actSt04lCrest2Main_ntsc(GObj *volatile self)
{
    if (current_stage_no == 19) {
        while (scpIsTorchLightOn(1030) == 0 || scpIsTorchLightOn(1031) == 0 || gflagChk(159) == 0) {
            _ACTWait(1);
        }
    }

    if (current_stage_no == 20) {
        while (scpIsTorchLightOn(1108) == 0 || scpIsTorchLightOn(1109) == 0 || gflagChk(159) == 0) {
            _ACTWait(1);
        }
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(25, &crest2, 1, 1, 1);

    while (crest2 == 0) {
        _ACTWait(1);
    }

    gflagOn(156);

    if (current_stage_no == 19) {
        stage_SetAnimation(179, 1, 0);

        while (stage_CheckAnimationFrame(179, 30, 0) == 0) {
            _ACTWait(1);
        }

        _ACTWait(1);

        st04l_yure = iosPadActRequest(boyPad, 10);
        st04l_yure_vol = 128;
        iosPadActVolumeSet(st04l_yure, 128);

        while (stage_CheckAnimationFrame(179, 190, 0) == 0) {
            _ACTWait(1);
        }

        _ACTWait(1);
        iosPadActStop(st04l_yure);

        while (stage_CheckAnimationFinish(179) == 0) {
            _ACTWait(1);
        }

        _ACTWait(1);
        scpSearchGobj(1034)->active = 1;
    } else {
        stage_SetAnimation(180, 1, 0);

        while (stage_CheckAnimationFrame(180, 30, 0) == 0) {
            _ACTWait(1);
        }

        _ACTWait(1);

        st04l_yure = iosPadActRequest(boyPad, 10);
        st04l_yure_vol = 128;
        iosPadActVolumeSet(st04l_yure, 128);

        while (stage_CheckAnimationFrame(180, 190, 0) == 0) {
            _ACTWait(1);
        }

        _ACTWait(1);
        iosPadActStop(st04l_yure);

        while (stage_CheckAnimationFinish(180) == 0) {
            _ACTWait(1);
        }

        _ACTWait(1);
    }

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
}

static void actSt04lCrest2Main_aug6jun26(GObj *volatile self)
{
    if (current_stage_no == 19) {
        while (scpIsTorchLightOn(1027) == 0 || scpIsTorchLightOn(1028) == 0 || gflagChk(159) == 0) {
            _ACTWait(1);
        }
    }

    if (current_stage_no == 20) {
        while (scpIsTorchLightOn(1105) == 0 || scpIsTorchLightOn(1106) == 0 || gflagChk(159) == 0) {
            _ACTWait(1);
        }
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(25, &crest2, 1, 1, 1);

    while (crest2 == 0) {
        _ACTWait(1);
    }

    gflagOn(156);

    if (current_stage_no == 19) {
        stage_SetAnimation(178, 1, 0);

        while (stage_CheckAnimationFrame(178, 30, 0) == 0) {
            _ACTWait(1);
        }

        _ACTWait(1);

        st04l_yure = iosPadActRequest(boyPad, 10);
        st04l_yure_vol = 128;
        iosPadActVolumeSet(st04l_yure, 128);

        while (stage_CheckAnimationFrame(178, 190, 0) == 0) {
            _ACTWait(1);
        }

        _ACTWait(1);
        iosPadActStop(st04l_yure);

        while (stage_CheckAnimationFinish(178) == 0) {
            _ACTWait(1);
        }

        _ACTWait(1);
        scpSearchGobj(1031)->active = 1;
    } else {
        stage_SetAnimation(179, 1, 0);

        while (stage_CheckAnimationFrame(179, 30, 0) == 0) {
            _ACTWait(1);
        }

        _ACTWait(1);

        st04l_yure = iosPadActRequest(boyPad, 10);
        st04l_yure_vol = 128;
        iosPadActVolumeSet(st04l_yure, 128);

        while (stage_CheckAnimationFrame(179, 190, 0) == 0) {
            _ACTWait(1);
        }

        _ACTWait(1);
        iosPadActStop(st04l_yure);

        while (stage_CheckAnimationFinish(179) == 0) {
            _ACTWait(1);
        }

        _ACTWait(1);
    }

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
}

void actSt04lCrest2Main(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lCrest2Main_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lCrest2Main_aug6jun26(self);
        return;
    }
    GProc *h;

    if (current_stage_no == 19) {
        crest2Anim = PAL_JPNKORCN(201, 197);

        while (scpIsTorchLightOn(1118) == 0 || scpIsTorchLightOn(1119) == 0 || gflagChk(178) == 0) {
            _ACTWait(1);
        }
    }

    if (current_stage_no == 20) {
        crest2Anim = PAL_JPNKORCN(202, 198);

        while (scpIsTorchLightOn(1202) == 0 || scpIsTorchLightOn(1203) == 0 || gflagChk(178) == 0) {
            _ACTWait(1);
        }
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(26, &ball3_4l, 1, 1, 1);

    while (ball3_4l == 0) {
        _ACTWait(1);
    }

    gflagOn(175);

    h = actCreateSubThread(actSt04lCrest2Sub, 21);

    demoEnd = 0;
    oriup4c = 0xFFFFFFFF;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&h->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        scpAdpcmFadeCloseFunc(&ball3_4l, 256);
        iosPadActStop(oriup4c);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(crest2Anim, 0, -1);

        if (current_stage_no == 19) {
            scpSearchGobj(1122)->active = 1;
        }

        SetCameraFlag_LwsCutBack();
        scpFadeIn(3.0f);
    }

    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
}

static void actSt04lCrest3Sub(GObj *volatile self)
{
    while (stage_CheckAnimationFrame(PAL_JPNKORCN(203, 199), 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    oriup4c = iosPadActRequest(boyPad, 10);
    oridown4c = 128;
    iosPadActVolumeSet(oriup4c, 128);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(203, 199), 190, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActStop(oriup4c);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(203, 199)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

static void actSt04lCrest3Main_ntsc(GObj *volatile self)
{
    while (scpIsTorchLightOn(1110) == 0 || scpIsTorchLightOn(1111) == 0 || gflagChk(160) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(23, &crest3, 0, 1, 1);

    while (crest3 == 0) {
        _ACTWait(1);
    }

    gflagOn(157);
    stage_SetAnimation(181, 1, 0);

    while (stage_CheckAnimationFrame(181, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    st04l_yure = iosPadActRequest(boyPad, 10);
    st04l_yure_vol = 128;
    iosPadActVolumeSet(st04l_yure, 128);

    while (stage_CheckAnimationFrame(181, 190, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActStop(st04l_yure);

    while (stage_CheckAnimationFinish(181) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
}

static void actSt04lCrest3Main_aug6jun26(GObj *volatile self)
{
    while (scpIsTorchLightOn(1107) == 0 || scpIsTorchLightOn(1108) == 0 || gflagChk(160) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(23, &crest3, 0, 1, 1);

    while (crest3 == 0) {
        _ACTWait(1);
    }

    gflagOn(157);
    stage_SetAnimation(180, 1, 0);

    while (stage_CheckAnimationFrame(180, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    st04l_yure = iosPadActRequest(boyPad, 10);
    st04l_yure_vol = 128;
    iosPadActVolumeSet(st04l_yure, 128);

    while (stage_CheckAnimationFrame(180, 190, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActStop(st04l_yure);

    while (stage_CheckAnimationFinish(180) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
}

void actSt04lCrest3Main(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lCrest3Main_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lCrest3Main_aug6jun26(self);
        return;
    }
    GProc *h;

    while (scpIsTorchLightOn(1204) == 0 || scpIsTorchLightOn(1205) == 0 || gflagChk(179) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(24, &crest1, 0, 1, 1);

    while (crest1 == 0) {
        _ACTWait(1);
    }

    gflagOn(176);
    stage_SetAnimation(PAL_JPNKORCN(203, 199), 1, 0);

    h = actCreateSubThread(actSt04lCrest3Sub, 21);

    demoEnd = 0;
    oriup4c = 0xFFFFFFFF;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&h->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        scpAdpcmFadeCloseFunc(&crest1, 256);
        iosPadActStop(oriup4c);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(PAL_JPNKORCN(203, 199), 0, -1);
        scpFadeIn(3.0f);
    }

    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpBoyControlReadDisable = 0;
    scpWakeupEnemyAll();
}

static void actSt04eSolarBeamChk_ntsc(GObj *volatile self)
{
    gflagOff(204);
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    scpAdpcmPlayRequestFunc(27, &solar4l, 0, 1, 1);

    while (solar4l == 0) {
        _ACTWait(1);
    }

    preload(7);
    scpFadeIn(6.0f);

    stage_SetAnimation(259, 1, 0);

    while (stage_CheckAnimationFinish(259) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    RequestStageChange(7, boyGObj, 0, 2.0f, 8.0f);
}

static void actSt04eSolarBeamChk_aug6jun26(GObj *volatile self)
{
    gflagOff(204);
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    scpAdpcmPlayRequestFunc(27, &solar4l, 0, 1, 1);

    while (solar4l == 0) {
        _ACTWait(1);
    }

    preload(7);
    scpFadeIn(6.0f);

    stage_SetAnimation(258, 1, 0);

    while (stage_CheckAnimationFinish(258) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    RequestStageChange(7, boyGObj, girlGObj, 2.0f, 8.0f);
}

void actSt04eSolarBeamChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04eSolarBeamChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04eSolarBeamChk_aug6jun26(self);
        return;
    }
    GProc *h;

    gflagOff(224);
    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;

    scpAdpcmPlayRequestFunc(28, &crest2, 0, 1, 1);

    while (crest2 == 0) {
        _ACTWait(1);
    }

    preload(7);
    scpFadeIn(16.0f);

    h = actCreateSubThread(actSt04eSolarBeamChkSub, 21);

    demoEnd = 0;

    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&h->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        scpAdpcmFadeCloseFunc(&crest2, 256);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(PAL_JPNKORCN(292, 288), 0, -1);
        scpFadeIn(3.0f);
    }

    RequestStageChange(7, boyGObj, 0, 2.0f, 8.0f);
}

static void actSt04lStairSub(GObj *volatile self)
{
    long long ofs[2];
    float dir[4];

    _ACTWait(60);

    while (crest3 == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(crest3->stream);

    stage_SetAnimation(PAL_JPNKORCN(258, 254), 1, 0);

    if (!ICO_JPNKORCN) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
    }
    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 532);
    scpPlayPosSet(boyGObj, 55.0f, 28.0f, -3881.0f);
    scpPlayPosSet(girlGObj, -58.0f, 28.0f, -3891.0f);

    ofs[0] = stairSubPos.d[0];
    ofs[1] = stairSubPos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    ofs[0] = stairSubPos.d[0];
    ofs[1] = stairSubPos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    subStarted = 1;

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(258, 254)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 17);

    stage_SetAnimation(PAL_JPNKORCN(227, 223), 1, 0);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(227, 223), 140, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    oriup4c = iosPadActRequest(boyPad, 9);
    oridown4c = 128;
    iosPadActVolumeSet(oriup4c, 128);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(227, 223)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoEnd = 1;
    _ACTWait(0);
}

static void actSt04lStairChk_ntsc(GObj *volatile self)
{
    long long ofs[2];
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0xB000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0xB000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyOne(3453);
    gflagOn(161);

    scpSearchGobj(1148)->active = 0;

    stage_SetAnimation(231, -1, -2);

    _ACTWait(60);

    scpAdpcmPlayRequestFunc(59, &stair4d, 1, 1, 1);

    while (stair4d == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(230, 1, 0);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);
    scpPlayPosSet(boyGObj, 55.0f, 28.0f, -3881.0f);
    scpPlayPosSet(girlGObj, -58.0f, 28.0f, -3891.0f);

    ofs[0] = stairSubPos.d[0];
    ofs[1] = stairSubPos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    ofs[0] = stairSubPos.d[0];
    ofs[1] = stairSubPos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    while (stage_CheckAnimationFinish(230) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 17);

    stage_SetAnimation(199, 1, 0);

    while (stage_CheckAnimationFrame(199, 140, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    st04l_yure = iosPadActRequest(boyPad, 9);
    st04l_yure_vol = 128;
    iosPadActVolumeSet(st04l_yure, 128);

    while (stage_CheckAnimationFinish(199) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(st04l_yure);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    gflagOn(200);
    scpWakeupEnemyOne(3453);
}

static void actSt04lStairChk_aug6jun26(GObj *volatile self)
{
    long long ofs[2];
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0xB000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0xB000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyOne(3415);
    gflagOn(161);

    scpSearchGobj(1145)->active = 0;

    stage_SetAnimation(230, -1, -2);

    _ACTWait(60);

    scpAdpcmPlayRequestFunc(59, &stair4d, 1, 1, 1);

    while (stair4d == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(229, 1, 0);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);
    scpPlayPosSet(boyGObj, 55.0f, 28.0f, -3881.0f);
    scpPlayPosSet(girlGObj, -58.0f, 28.0f, -3891.0f);

    ofs[0] = stairSubPos.d[0];
    ofs[1] = stairSubPos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    ofs[0] = stairSubPos.d[0];
    ofs[1] = stairSubPos.d[1];
    sceVu0SubVector(dir, ofs, test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    while (stage_CheckAnimationFinish(229) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActRequest(boyPad, 17);

    stage_SetAnimation(198, 1, 0);

    while (stage_CheckAnimationFrame(198, 140, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    st04l_yure = iosPadActRequest(boyPad, 9);
    st04l_yure_vol = 128;
    iosPadActVolumeSet(st04l_yure, 128);

    while (stage_CheckAnimationFinish(198) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(st04l_yure);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    gflagOn(200);
    scpWakeupEnemyOne(3415);
}

void actSt04lStairChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lStairChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lStairChk_aug6jun26(self);
        return;
    }
    GProc *h;

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0xB000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0xB000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    if (ICO_JPNKORCN) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpSleepEnemyOne(3734);
    } else {
        scpSleepEnemyOne(3757);
    }
    gflagOn(180);
    FinishHint(17);

    scpSearchGobj(1242)->active = 0;

    stage_SetAnimation(PAL_JPNKORCN(259, 255), -1, -2);

    scpAdpcmPlayRequestFunc(61, &crest3, 1, 1, 0);

    h = actCreateSubThread(actSt04lStairSub, 21);

    demoEnd = 0;
    subStarted = 0;
    oriup4c = 0xFFFFFFFF;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        while (crest3 == 0) {
            _ACTWait(1);
        }

        scpAdpcmFadeCloseFunc(&crest3, 512);

        while (subStarted == 0) {
            _ACTWait(1);
        }

        iosThreadSetPri(&h->thread, 34);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(PAL_JPNKORCN(258, 254), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(227, 223), 0, -1);

        _ACTWait(1);

        scpPlayMot(boyGObj, 0);
        scpPlayMot(girlGObj, 532);
        scpPlayPosSet(boyGObj, 55.0f, 234.0f, -3881.0f);
        scpPlayPosSet(girlGObj, -58.0f, 234.0f, -3891.0f);

        _ACTWait(1);
        iosOmSendMail(girlGObj, 62, boyGObj);

        scpFadeIn(3.0f);
    } else {
        iosThreadSetPri(&h->thread, 34);
    }

    iosPadActStop(oriup4c);

    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 532);
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);

    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
    gflagOn(220);
    scpWakeupEnemyOne(PAL_JPNKORCN(3757, 3734));
}

void actSt04lRope1Chk(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(PAL_NTSCAUG6JUN26(184, 165)) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(
            scpSearchGobj(PAL_NTSC_AUG6JUN26(1183, 1088, 1085)))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(220, 216, 192, 191), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(220, 216, 192, 191)) ==
                   0) {
                _ACTWait(1);
            }

            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(PAL_NTSC_AUG6JUN26(1183, 1088, 1085))->active = 0;
            gflagOn(PAL_NTSCAUG6JUN26(184, 165));
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(216, 212, 188, 187), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(216, 212, 188, 187)) ==
                   0) {
                _ACTWait(1);
            }

            _ACTWait(1);
            break;
        }
    }
}

void actSt04lRope2Chk(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(PAL_NTSCAUG6JUN26(185, 166)) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(
            scpSearchGobj(PAL_NTSC_AUG6JUN26(1184, 1089, 1086)))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(221, 217, 193, 192), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(221, 217, 193, 192)) ==
                   0) {
                _ACTWait(1);
            }

            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(PAL_NTSC_AUG6JUN26(1184, 1089, 1086))->active = 0;
            gflagOn(PAL_NTSCAUG6JUN26(185, 166));
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(217, 213, 189, 188), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(217, 213, 189, 188)) ==
                   0) {
                _ACTWait(1);
            }

            _ACTWait(1);
            break;
        }
    }
}

void actSt04lRope3Chk(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(PAL_NTSCAUG6JUN26(186, 167)) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(
            scpSearchGobj(PAL_NTSC_AUG6JUN26(1185, 1090, 1087)))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(222, 218, 194, 193), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(222, 218, 194, 193)) ==
                   0) {
                _ACTWait(1);
            }

            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(PAL_NTSC_AUG6JUN26(1185, 1090, 1087))->active = 0;
            gflagOn(PAL_NTSCAUG6JUN26(186, 167));
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(218, 214, 190, 189), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(218, 214, 190, 189)) ==
                   0) {
                _ACTWait(1);
            }

            _ACTWait(1);
            break;
        }
    }
}

void actSt04lRope4Chk(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (gflagChk(PAL_NTSCAUG6JUN26(187, 168)) == 0) {
        switch (GetAttackCheckBoundaryManagerStatus(
            scpSearchGobj(PAL_NTSC_AUG6JUN26(1186, 1091, 1088)))) {
        case 0:
            _ACTWait(1);
            break;
        case 1:
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(223, 219, 195, 194), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(223, 219, 195, 194)) ==
                   0) {
                _ACTWait(1);
            }

            _ACTWait(1);
            break;
        case 2:
            scpSearchGobj(PAL_NTSC_AUG6JUN26(1186, 1091, 1088))->active = 0;
            gflagOn(PAL_NTSCAUG6JUN26(187, 168));
            stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(219, 215, 191, 190), 1, 0);

            while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(219, 215, 191, 190)) ==
                   0) {
                _ACTWait(1);
            }

            _ACTWait(1);
            break;
        }
    }
}

void actSt04lSekizoChk(GObj *volatile self)
{
    float dir[4];

    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, boyGObj, 200.0f) == 0 ||
           scpTriggerBall(self, girlGObj, 200.0f) == 0 ||
           scpTriggerFloorAttr(boyGObj, 0x8000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x8000000) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSC) {
        lt_switch_layout(51);
        scpBoyControlReadDisable = 1;
    } else if (ICO_AUG6JUN26) {
        scpAdpcmPlayRequestFunc(18, &sekizo4c, 1, 1, 1);

        while (sekizo4c == 0) {
            _ACTWait(1);
        }
    } else {
        lt_switch_layout(PAL_JPNKORCN(55, 52));
        scpBoyControlReadDisable = 1;
    }
    scpKillEnemyAll();
    scpMaskGeneratorAll();

    if (ICO_NTSC) {
        scpAdpcmPlayRequestFunc(18, &sekizo4c, 1, 1, 1);

        while (sekizo4c == 0) {
            _ACTWait(1);
        }
    } else if (ICO_AUG6JUN26) {
        lt_switch_layout(51);
        scpBoyControlReadDisable = 1;
    } else {
        scpAdpcmPlayRequestFunc(18, &stair4d, 1, 1, 1);

        while (stair4d == 0) {
            _ACTWait(1);
        }
    }

    SetWayGroupActive(1, 1);

    if (ICO_NTSCAUG6JUN26) {
        _ACTWait(60);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(226, 222), 1, 0);
    }

    if (ICO_NTSC) {
        stage_SetAnimation(198, 1, 0);

        sekizo_4l = iosPadActRequest(boyPad, 9);
        sekizo_4l_vol = 128;
        iosPadActVolumeSet(sekizo_4l, 128);
    } else if (ICO_AUG6JUN26) {
        stage_SetAnimation(197, 1, 0);

        sekizo_4l = iosPadActRequest(boyPad, 9);
        sekizo_4l_vol = 128;
        iosPadActVolumeSet(sekizo_4l, 128);
    } else {
        st04l_yure = iosPadActRequest(boyPad, 9);
        st04l_yure_vol = 128;
        iosPadActVolumeSet(st04l_yure, 128);
    }

    scpPlayStart(boyGObj);
    scpPlayStart(girlGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, PAL_NTSCAUG6JUN26(532, 514));
    scpPlayPosSet(girlGObj, 0.0f, -1300.0f, -1700.0f);
    scpPlayPosSet(boyGObj, 20.0f, -1300.0f, -1700.0f);

    _ACTWait(1);

    sceVu0SubVector(dir, test_CURRENTROOT((void *)self), test_CURRENTROOT(girlGObj));
    scpPlayMotDir(girlGObj, dir);

    scpBoyControlReadDisable = 1;

    sceVu0SubVector(dir, test_CURRENTROOT(girlGObj), test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir);

    scpSekizouCheckPoint();

    scpPlayMot(girlGObj, PAL_NTSC_AUG6JUN26(645, 572, 573));
    scpPlayWaitMotEnd(girlGObj);

    gflagOn(PAL_NTSCAUG6JUN26(188, 169));

    while (stage_CheckAnimationFrame(PAL_JPNKORCN_NTSC_AUG6JUN26(226, 222, 198, 197), 151, 0) ==
           0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    iosPadActStop(PAL_NTSCAUG6JUN26(st04l_yure, sekizo_4l));

    if (ICO_NTSC) {
        while (stage_CheckAnimationFinish(198) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        scpPlayMot(girlGObj, 514);
    } else if (ICO_AUG6JUN26) {
        scpPlayMot(girlGObj, 514);
    } else {
        while (stage_CheckAnimationFinish(PAL_JPNKORCN(226, 222)) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        scpPlayMot(girlGObj, 532);
    }
    scpPlayEnd(girlGObj);
    scpPlayMot(boyGObj, 0);
    scpPlayEnd(boyGObj);

    if (ICO_NTSC) {
        _ACTWait(1);
        iosOmSendMail(girlGObj, 60, boyGObj);

        lt_switch_layout(50);
    } else if (ICO_AUG6JUN26) {
        lt_switch_layout(50);
    } else {
        _ACTWait(1);
        iosOmSendMail(girlGObj, 63, boyGObj);

        lt_switch_layout(PAL_JPNKORCN(54, 51));
    }
    scpBoyControlReadDisable = 0;
}

static void actSt04lGondolaChk_ntsc(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0xA000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();
    _ACTWait(15);

    if (gflagChk(171) != 0) {
        stage_SetAnimation(200, 1, 200);

        while (stage_CheckAnimationFrame(200, 220, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1297, 0, 0, 1);

        while (stage_CheckAnimationFrame(200, 240, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1298, 0, 0, 1);

        while (stage_CheckAnimationFrame(200, 250, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1295, 0, 0, 1);

        while (stage_CheckAnimationFrame(200, 415, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1296, 0, 0, 1);

        while (stage_CheckAnimationFinish(200) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gflagOff(171);
    } else {
        stage_SetAnimation(200, 1, 0);
        soundSeDefPlay(1295, 0, 0, 1);

        while (stage_CheckAnimationFrame(200, 145, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1296, 0, 0, 1);
        soundSeDefPlay(1297, 0, 0, 1);

        while (stage_CheckAnimationFrame(200, 170, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1298, 0, 0, 1);

        while (stage_CheckAnimationFrame(200, 200, 1) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gflagOn(171);
    }

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    scpWakeupEnemyAll();

    gondolaChk_mes[0].func = actSt04lGondolaCharaChk;
    sub->mail = gondolaChk_mes;
    ACTSendMailCorrect(self, 397);
    _ACTWait(0);
}

static void actSt04lGondolaChk_aug6jun26(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0xA000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();
    _ACTWait(15);

    if (gflagChk(171) != 0) {
        stage_SetAnimation(199, 1, 200);

        while (stage_CheckAnimationFrame(199, 220, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1297, 0, 0, 1);

        while (stage_CheckAnimationFrame(199, 240, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1298, 0, 0, 1);

        while (stage_CheckAnimationFrame(199, 250, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1295, 0, 0, 1);

        while (stage_CheckAnimationFrame(199, 415, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1296, 0, 0, 1);

        while (stage_CheckAnimationFinish(199) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gflagOff(171);
    } else {
        stage_SetAnimation(199, 1, 0);
        soundSeDefPlay(1295, 0, 0, 1);

        while (stage_CheckAnimationFrame(199, 145, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1296, 0, 0, 1);
        soundSeDefPlay(1297, 0, 0, 1);

        while (stage_CheckAnimationFrame(199, 170, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1298, 0, 0, 1);

        while (stage_CheckAnimationFrame(199, 200, 1) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gflagOn(171);
    }

    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    scpWakeupEnemyAll();

    gondolaChk_mes[0].func = actSt04lGondolaCharaChk;
    sub->mail = gondolaChk_mes;
    ACTSendMailCorrect(self, 393);
    _ACTWait(0);
}

void actSt04lGondolaChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lGondolaChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lGondolaChk_aug6jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0xA000000) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();
    _ACTWait(15);

    if (gflagChk(190) != 0) {
        stage_SetAnimation(PAL_JPNKORCN(228, 224), 1, 200);

        while (stage_CheckAnimationFrame(PAL_JPNKORCN(228, 224), 220, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1321, 0, 0, 1);

        while (stage_CheckAnimationFrame(PAL_JPNKORCN(228, 224), 240, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1322, 0, 0, 1);

        while (stage_CheckAnimationFrame(PAL_JPNKORCN(228, 224), 250, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1319, 0, 0, 1);

        while (stage_CheckAnimationFrame(PAL_JPNKORCN(228, 224), 415, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1320, 0, 0, 1);

        while (stage_CheckAnimationFinish(PAL_JPNKORCN(228, 224)) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        gflagOff(190);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(228, 224), 1, 0);
        SetGirlDangerGObj(boyGObj);
        soundSeDefPlay(1319, 0, 0, 1);

        while (stage_CheckAnimationFrame(PAL_JPNKORCN(228, 224), 145, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1320, 0, 0, 1);
        soundSeDefPlay(1321, 0, 0, 1);

        while (stage_CheckAnimationFrame(PAL_JPNKORCN(228, 224), 170, 0) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);
        soundSeDefPlay(1322, 0, 0, 1);

        while (stage_CheckAnimationFrame(PAL_JPNKORCN(228, 224), 200, 1) == 0) {
            _ACTWait(1);
        }
        _ACTWait(1);

        ClearGirlDangerGObj();
        gflagOn(190);
    }

    if (girlGObj != 0 && scpTriggerFloorAttr(girlGObj, 0xA000000) == 0) {
        gflagOn(194);
    } else {
        gflagOff(194);
    }

    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
    scpWakeupEnemyAll();

    gondolaChk_mes[0].func = actSt04lGondolaCharaChk;
    sub->mail = gondolaChk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt04lMonyou01Chk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(192);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(206, 1, 0);
    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(206, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 60, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04lMonyou01Chk_aug6jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);
    gflagOn(192);
    scpSleepEnemyAll();
    _ACTWait(5);

    stage_SetAnimation(205, 1, 0);
    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(205, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;
}

void actSt04lMonyou01Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lMonyou01Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lMonyou01Chk_aug6jun26(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(212);
    FinishHint(16);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKORCN(234, 230), 1, 0);
    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(234, 230), 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04lMonyou02Chk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x2000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x2000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(193);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(207, 1, 0);
    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(207, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 60, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04lMonyou02Chk_aug6jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x2000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x2000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);
    gflagOn(193);
    scpSleepEnemyAll();
    _ACTWait(5);

    stage_SetAnimation(206, 1, 0);
    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(206, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;
}

void actSt04lMonyou02Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lMonyou02Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lMonyou02Chk_aug6jun26(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x2000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x2000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(213);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKORCN(235, 231), 1, 0);
    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(235, 231), 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04lMonyou03Chk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x3000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(194);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(208, 1, 0);
    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(208, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 60, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04lMonyou03Chk_aug6jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x3000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);
    gflagOn(194);
    scpSleepEnemyAll();
    _ACTWait(5);

    stage_SetAnimation(207, 1, 0);
    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(207, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;
}

void actSt04lMonyou03Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lMonyou03Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lMonyou03Chk_aug6jun26(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x3000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x3000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(214);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKORCN(236, 232), 1, 0);
    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(236, 232), 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04lMonyou04Chk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x4000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x4000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(195);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(209, 1, 0);
    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(209, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 60, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04lMonyou04Chk_aug6jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x4000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x4000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);
    gflagOn(195);
    scpSleepEnemyAll();
    _ACTWait(5);

    stage_SetAnimation(208, 1, 0);
    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(208, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;
}

void actSt04lMonyou04Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lMonyou04Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lMonyou04Chk_aug6jun26(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x4000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x4000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(215);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKORCN(237, 233), 1, 0);
    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(237, 233), 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04lMonyou05Chk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x5000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x5000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(196);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(210, 1, 0);
    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(210, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 60, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04lMonyou05Chk_aug6jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x5000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x5000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);
    gflagOn(196);
    scpSleepEnemyAll();
    _ACTWait(5);

    stage_SetAnimation(209, 1, 0);
    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(209, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;
}

void actSt04lMonyou05Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lMonyou05Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lMonyou05Chk_aug6jun26(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x5000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x5000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(216);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKORCN(238, 234), 1, 0);
    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(238, 234), 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04lMonyou06Chk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x6000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x6000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(197);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(211, 1, 0);
    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(211, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 60, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04lMonyou06Chk_aug6jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x6000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x6000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);
    gflagOn(197);
    scpSleepEnemyAll();
    _ACTWait(5);

    stage_SetAnimation(210, 1, 0);
    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(210, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;
}

void actSt04lMonyou06Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lMonyou06Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lMonyou06Chk_aug6jun26(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x6000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x6000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(217);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKORCN(239, 235), 1, 0);
    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(239, 235), 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04lMonyou07Chk_ntsc(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x7000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x7000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(198);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(212, 1, 0);
    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(212, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 60, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04lMonyou07Chk_aug6jun26(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x7000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x7000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    scpPlayMot(boyGObj, 0);
    scpPlayMot(girlGObj, 514);
    gflagOn(198);
    scpSleepEnemyAll();
    _ACTWait(5);

    stage_SetAnimation(211, 1, 0);
    soundSeDefPlay(1308, 0, 0, 1);

    while (stage_CheckAnimationFrame(211, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();

    scpBoyControlReadDisable = 0;
}

void actSt04lMonyou07Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lMonyou07Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lMonyou07Chk_aug6jun26(self);
        return;
    }
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerFloorAttr(boyGObj, 0x7000000) == 0 ||
           scpTriggerFloorAttr(girlGObj, 0x7000000) == 0) {
        _ACTWait(1);
    }

    scpBoyControlReadDisable = 1;
    gflagOn(218);
    scpSleepEnemyAll();

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        scpPlayStart(boyGObj);
        scpPlayStart(girlGObj);
        scpPlayMotReq(boyGObj, 1);
        scpPlayMotReq(girlGObj, 1);
        _ACTWait(1);
        ACTGame_ConnectHand();
    }

    stage_SetAnimation(PAL_JPNKORCN(240, 236), 1, 0);
    soundSeDefPlay(1332, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(240, 236), 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpPlayEnd(boyGObj);
    scpPlayEnd(girlGObj);
    _ACTWait(1);

    if (ACTGame_FLAG_TETSUNAGI() != 0) {
        iosOmSendMail(girlGObj, 63, boyGObj);
    }

    scpBoyControlReadDisable = 0;
}

static void actSt04lOriSub(GObj *volatile self)
{
    _ACTWait(60);

    while (st04d_hasi == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(st04d_hasi->stream);

    stage_SetAnimation(PAL_JPNKORCN(243, 239), 1, 0);
    stage_SetAnimation(PAL_JPNKORCN(245, 241), 1, 0);
    stage_SetAnimation(PAL_JPNKORCN(244, 240), 1, 0);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(243, 239), 90, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActRequest(boyPad, 15);

    if (girlGObj != 0) {
        scpPlayMotReq(girlGObj, 216);
    }

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(243, 239)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

static void actSt04lOriChk_ntsc(GObj *volatile self)
{
    while (gflagChk(156) == 0 || scpGameStat_BoyWeaponkind() != 4 ||
           scpTriggerBall(self, boyGObj, 1000.0f) == 0) {
        _ACTWait(1);
    }

    scpSleepEnemyAll();
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    scpPlayMot(boyGObj, 0);

    if (girlGObj != 0) {
        iosOmSendMail(girlGObj, 59, boyGObj);
    }

    gflagOn(180);

    _ACTWait(60);

    scpAdpcmPlayRequestFunc(60, &oriup4c_ntscaug6jun26, 1, 1, 1);

    while (oriup4c_ntscaug6jun26 == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(215, 1, 0);
    stage_SetAnimation(217, 1, 0);
    stage_SetAnimation(216, 1, 0);

    while (stage_CheckAnimationFrame(215, 90, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActRequest(boyPad, 15);

    while (stage_CheckAnimationFinish(215) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static void actSt04lOriChk_aug6jun26(GObj *volatile self)
{
    while (gflagChk(156) == 0 || scpGameStat_BoyWeaponkind() != 4 ||
           scpTriggerBall(self, boyGObj, 1000.0f) == 0) {
        _ACTWait(1);
    }

    scpSleepEnemyAll();
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;

    scpPlayMot(boyGObj, 0);

    if (girlGObj != 0) {
        iosOmSendMail(girlGObj, 58, boyGObj);
    }

    gflagOn(180);

    _ACTWait(60);

    scpAdpcmPlayRequestFunc(60, &oriup4c_ntscaug6jun26, 1, 1, 1);

    while (oriup4c_ntscaug6jun26 == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(214, 1, 0);
    stage_SetAnimation(216, 1, 0);
    stage_SetAnimation(215, 1, 0);

    while (stage_CheckAnimationFrame(214, 90, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActRequest(boyPad, 15);

    while (stage_CheckAnimationFinish(214) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actSt04lOriChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lOriChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lOriChk_aug6jun26(self);
        return;
    }
    GProc *h;

    while (gflagChk(175) == 0 || scpGameStat_BoyWeaponkind() != 4 ||
           scpTriggerBall(self, boyGObj, 1000.0f) == 0) {
        _ACTWait(1);
    }

    scpSleepEnemyAll();
    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;

    scpPlayMot(boyGObj, 0);

    if (girlGObj != 0) {
        iosOmSendMail(girlGObj, 62, boyGObj);
    }

    gflagOn(200);
    SetWayGroupActive(14, 0);
    SetWayGroupActive(15, 0);

    scpAdpcmPlayRequestFunc(62, &st04d_hasi, 1, 1, 0);

    h = actCreateSubThread(actSt04lOriSub, 21);

    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&h->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        while (st04d_hasi == 0) {
            _ACTWait(1);
        }

        scpAdpcmFadeCloseFunc(&st04d_hasi, 512);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(PAL_JPNKORCN(243, 239), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(245, 241), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(244, 240), 0, -1);
        scpFadeIn(3.0f);
    }

    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

static void actSt04lOriRopeCutRChk_ntsc(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (1) {
        if (scpGameStat_BoyWeaponkind() == 4 && scpTriggerBall(self, boyGObj, 100.0f) != 0 &&
            (ForMotionViewer_GetCurrentMotion(boyGObj) == 37 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 42 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 44 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 38 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 39 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 60))
            break;
        _ACTWait(1);
    }

    gflagOn(181);

    stage_SetAnimation(218, 1, 0);
    soundSeDefPlay(1299, 0, 0, 1);

    while (stage_CheckAnimationFrame(218, 45, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    soundSeDefPlay(1300, 0, 0, 1);

    while (stage_CheckAnimationFinish(218) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
}

static void actSt04lOriRopeCutRChk_aug6jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (1) {
        if (scpGameStat_BoyWeaponkind() == 4 && scpTriggerBall(self, boyGObj, 100.0f) != 0 &&
            (ForMotionViewer_GetCurrentMotion(boyGObj) == 37 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 42 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 44 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 38 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 39 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 60))
            break;
        _ACTWait(1);
    }

    gflagOn(181);

    stage_SetAnimation(217, 1, 0);
    soundSeDefPlay(1299, 0, 0, 1);

    while (stage_CheckAnimationFrame(217, 45, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    soundSeDefPlay(1300, 0, 0, 1);

    while (stage_CheckAnimationFinish(217) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
}

void actSt04lOriRopeCutRChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lOriRopeCutRChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lOriRopeCutRChk_aug6jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (1) {
        if (scpGameStat_BoyWeaponkind() == 4 && scpTriggerBall(self, boyGObj, 100.0f) != 0 &&
            (ForMotionViewer_GetCurrentMotion(boyGObj) == 38 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 43 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 45 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 39 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 40 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 60))
            break;
        _ACTWait(1);
    }

    _ACTWait(15);
    gflagOn(201);

    stage_SetAnimation(PAL_JPNKORCN(246, 242), 1, 0);
    soundSeDefPlay(1323, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(246, 242), 32, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    soundSeDefPlay(1324, 0, 0, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(246, 242)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
}

static void actSt04lOriRopeCutLChk_ntsc(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (1) {
        if (scpGameStat_BoyWeaponkind() == 4 && scpTriggerBall(self, boyGObj, 100.0f) != 0 &&
            (ForMotionViewer_GetCurrentMotion(boyGObj) == 37 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 42 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 44 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 38 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 39 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 60))
            break;
        _ACTWait(1);
    }

    gflagOn(182);

    stage_SetAnimation(219, 1, 0);
    soundSeDefPlay(1299, 0, 0, 1);

    while (stage_CheckAnimationFrame(219, 45, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    soundSeDefPlay(1300, 0, 0, 1);

    while (stage_CheckAnimationFinish(219) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
}

static void actSt04lOriRopeCutLChk_aug6jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (1) {
        if (scpGameStat_BoyWeaponkind() == 4 && scpTriggerBall(self, boyGObj, 100.0f) != 0 &&
            (ForMotionViewer_GetCurrentMotion(boyGObj) == 37 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 42 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 44 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 38 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 39 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 60))
            break;
        _ACTWait(1);
    }

    gflagOn(182);

    stage_SetAnimation(218, 1, 0);
    soundSeDefPlay(1299, 0, 0, 1);

    while (stage_CheckAnimationFrame(218, 45, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    soundSeDefPlay(1300, 0, 0, 1);

    while (stage_CheckAnimationFinish(218) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
}

void actSt04lOriRopeCutLChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lOriRopeCutLChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lOriRopeCutLChk_aug6jun26(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    while (1) {
        if (scpGameStat_BoyWeaponkind() == 4 && scpTriggerBall(self, boyGObj, 100.0f) != 0 &&
            (ForMotionViewer_GetCurrentMotion(boyGObj) == 38 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 43 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 45 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 39 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 40 ||
             ForMotionViewer_GetCurrentMotion(boyGObj) == 60))
            break;
        _ACTWait(1);
    }

    _ACTWait(15);
    gflagOn(202);

    stage_SetAnimation(PAL_JPNKORCN(247, 243), 1, 0);
    soundSeDefPlay(1323, 0, 0, 1);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(247, 243), 32, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    soundSeDefPlay(1324, 0, 0, 1);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(247, 243)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
}

static void actSt04lOri2_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(183) == 0) {
        ori2_mes[0].func = actSt04lOri2Chk;
        act->mail = ori2_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        stage_SetAnimation(215, -1, -2);
        stage_SetAnimation(217, -1, -2);
        stage_SetAnimation(216, -1, -2);
        stage_SetAnimation(220, 0, -1);
        stage_SetAnimation(219, 0, -1);
        stage_SetAnimation(218, 0, -1);
    }
}

static void actSt04lOri2_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(183) == 0) {
        ori2_mes[0].func = actSt04lOri2Chk;
        act->mail = ori2_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    } else {
        stage_SetAnimation(214, -1, -2);
        stage_SetAnimation(216, -1, -2);
        stage_SetAnimation(215, -1, -2);
        stage_SetAnimation(219, 0, -1);
        stage_SetAnimation(218, 0, -1);
        stage_SetAnimation(217, 0, -1);
    }
}

void actSt04lOri2(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lOri2_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lOri2_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(203) == 0) {
        SleepHint(15);

        ori2_mes[0].func = actSt04lOri2Chk;
        act->mail = ori2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(243, 239), -1, -2);
        stage_SetAnimation(PAL_JPNKORCN(245, 241), -1, -2);
        stage_SetAnimation(PAL_JPNKORCN(244, 240), -1, -2);
        stage_SetAnimation(PAL_JPNKORCN(248, 244), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(247, 243), 0, -1);
        stage_SetAnimation(PAL_JPNKORCN(246, 242), 0, -1);
    }
}

static void actSt04lOri2Sub(GObj *volatile self)
{
    iosPadActRequest(boyPad, 17);
    _ACTWait(30);

    while (sekizo4c == 0) {
        _ACTWait(1);
    }

    AdpcmPlay(sekizo4c->stream);

    stage_SetAnimation(PAL_JPNKORCN(248, 244), 1, 0);

    while (stage_CheckAnimationFrame(PAL_JPNKORCN(248, 244), 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActRequest(boyPad, 15);

    if (girlGObj != 0) {
        scpPlayMotReq(girlGObj, 216);
    }

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(248, 244)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

static void actSt04lOri2Chk_ntsc(GObj *volatile self)
{
    while (gflagChk(181) == 0 || gflagChk(182) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    gflagOn(183);
    scpSleepEnemyAll();

    iosPadActRequest(boyPad, 17);
    _ACTWait(30);

    scpAdpcmPlayRequestFunc(61, &oridown4c_ntscaug6jun26, 1, 1, 1);

    while (oridown4c_ntscaug6jun26 == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(220, 1, 0);

    while (stage_CheckAnimationFrame(220, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActRequest(boyPad, 15);

    while (stage_CheckAnimationFinish(220) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static void actSt04lOri2Chk_aug6jun26(GObj *volatile self)
{
    while (gflagChk(181) == 0 || gflagChk(182) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    gflagOn(183);
    scpSleepEnemyAll();

    iosPadActRequest(boyPad, 17);
    _ACTWait(30);

    scpAdpcmPlayRequestFunc(61, &oridown4c_ntscaug6jun26, 1, 1, 1);

    while (oridown4c_ntscaug6jun26 == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(219, 1, 0);

    while (stage_CheckAnimationFrame(219, 30, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActRequest(boyPad, 15);

    while (stage_CheckAnimationFinish(219) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actSt04lOri2Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lOri2Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lOri2Chk_aug6jun26(self);
        return;
    }
    GProc *h;

    while (gflagChk(201) == 0 || gflagChk(202) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    gflagOn(203);
    WakeupHint(15);
    SetWayGroupActive(14, 1);
    SetWayGroupActive(15, 1);
    scpSleepEnemyAll();

    scpAdpcmPlayRequestFunc(63, &sekizo4c, 1, 1, 0);

    h = actCreateSubThread(actSt04lOri2Sub, 21);

    demoEnd = 0;

    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }

    iosThreadSetPri(&h->thread, 34);

    if (demoEnd == 0) {
        scpFadeOut(16.0f, 0, 0, 0);

        while (sekizo4c == 0) {
            _ACTWait(1);
        }

        scpAdpcmFadeCloseFunc(&sekizo4c, 512);

        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        while (lt_fade_status() != 2) {
            _ACTWait(1);
        }

        stage_SetAnimation(PAL_JPNKORCN(248, 244), 0, -1);
        scpFadeIn(3.0f);
    }

    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
}

void actSt04lCrest01(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(174, 155)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(199, 195, 177, 176), 0, 0);

        crest01_mes[0].func = actSt04lCrestMain;
        act->mail = crest01_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(199, 195, 177, 176), 0, -1);

        scpBoyControlReadDisable = 0;
    }
}

void actSt04lCrest03(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(176, 157)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(203, 199, 181, 180), 0, 0);

        crest03_mes[0].func = actSt04lCrest3Main;
        act->mail = crest03_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(203, 199, 181, 180), 0, -1);
    }
}

void actSt04lC1Ball(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(177, 158)) == 0) {
        scpSearchGobj(PAL_NTSC_AUG6JUN26(1114, 1026, 1023))->active = 0;
        scpSearchGobj(PAL_NTSC_AUG6JUN26(1115, 1027, 1024))->active = 0;

        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(207, 203, 185, 184), 0, 0);

        c1Ball_mes[0].func = actSt04lC1BallMain;
        act->mail = c1Ball_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(207, 203, 185, 184), 0, -1);
    }
}

void actSt04lC2Ball(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(178, 159)) == 0) {
        scpSearchGobj(PAL_NTSC_AUG6JUN26(1202, 1108, 1105))->active = 0;
        scpSearchGobj(PAL_NTSC_AUG6JUN26(1203, 1109, 1106))->active = 0;

        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(208, 204, 186, 185), 0, 0);

        c2Ball_mes[0].func = actSt04lC2BallMain;
        act->mail = c2Ball_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(208, 204, 186, 185), 0, -1);
    }
}

void actSt04lC3Ball(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(179, 160)) == 0) {
        scpSearchGobj(PAL_NTSC_AUG6JUN26(1204, 1110, 1107))->active = 0;
        scpSearchGobj(PAL_NTSC_AUG6JUN26(1205, 1111, 1108))->active = 0;

        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(209, 205, 187, 186), 0, 0);

        c3Ball_mes[0].func = actSt04lC3BallMain;
        act->mail = c3Ball_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(209, 205, 187, 186), 0, -1);
    }
}

void actSt04lStair(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(180, 161)) == 0) {
        if (girlGObj != 0) {
            stair_mes[0].func = actSt04lStairChk;
            act->mail = stair_mes;
            ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
            _ACTWait(0);
        }
    } else {
        scpSearchGobj(PAL_NTSC_AUG6JUN26(1242, 1148, 1145))->active = 0;

        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(259, 255, 231, 230), -1, -2);
    }
}

void actSt04lBrg1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(182, 163)) == 0) {
        brg1_mes[0].func = actSt04lBrg1Chk;
        act->mail = brg1_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

static void actSt04lBrg2_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(164) == 0) {
        brg2_mes[0].func = actSt04lBrg2Chk;
        act->mail = brg2_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actSt04lBrg2_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(164) == 0) {
        brg2_mes[0].func = actSt04lBrg2Chk;
        act->mail = brg2_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt04lBrg2(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lBrg2_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lBrg2_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(183) == 0) {
        SleepHint(16);

        brg2_mes[0].func = actSt04lBrg2Chk;
        act->mail = brg2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04lBrg1Way(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(182, 163)) == 0) {
        brg1Way_mes[0].func = actSt04lBrg1WayChk;
        act->mail = brg1Way_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt04lBrg2Way(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(183, 164)) == 0) {
        brg2Way_mes[0].func = actSt04lBrg2WayChk;
        act->mail = brg2Way_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt04lRope1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(184, 165)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(216, 212, 188, 187), 0, 0);

        rope1_mes[0].func = actSt04lRope1Chk;
        act->mail = rope1_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(216, 212, 188, 187), 0, -1);

        scpSearchGobj(PAL_NTSC_AUG6JUN26(1183, 1088, 1085))->active = 0;
    }
}

void actSt04lRope2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(185, 166)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(217, 213, 189, 188), 0, 0);

        rope2_mes[0].func = actSt04lRope2Chk;
        act->mail = rope2_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(217, 213, 189, 188), 0, -1);

        scpSearchGobj(PAL_NTSC_AUG6JUN26(1184, 1089, 1086))->active = 0;
    }
}

void actSt04lRope3(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(186, 167)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(218, 214, 190, 189), 0, 0);

        rope3_mes[0].func = actSt04lRope3Chk;
        act->mail = rope3_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(218, 214, 190, 189), 0, -1);

        scpSearchGobj(PAL_NTSC_AUG6JUN26(1185, 1090, 1087))->active = 0;
    }
}

void actSt04lRope4(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(187, 168)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(219, 215, 191, 190), 0, 0);

        rope4_mes[0].func = actSt04lRope4Chk;
        act->mail = rope4_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(219, 215, 191, 190), 0, -1);

        scpSearchGobj(PAL_NTSC_AUG6JUN26(1186, 1091, 1088))->active = 0;
    }
}

void actSt04lSekizo(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(188, 169)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(226, 222, 198, 197), 0, 0);

        sekizo_mes[0].func = actSt04lSekizoChk;
        act->mail = sekizo_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(226, 222, 198, 197), 0, -1);
    }
}

void actSt04lTuri(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(189, 170)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(230, 226, 202, 201), 0, 0);

        if (girlGObj != 0) {
            turi_mes[0].func = actSt04lTuriChk;
            act->mail = turi_mes;
            ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
            _ACTWait(0);
        }
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(230, 226, 202, 201), 0, -1);
    }
}

void actSt04lOri(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(200, 180)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(243, 239, 215, 214), 0, 0);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(245, 241, 217, 216), 0, 0);
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(244, 240, 216, 215), 0, 0);

        ori_mes[0].func = actSt04lOriChk;
        act->mail = ori_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt04lOriRopeCutR(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(201, 181)) == 0) {
        oriRopeCutR_mes[0].func = actSt04lOriRopeCutRChk;
        act->mail = oriRopeCutR_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt04lOriRopeCutL(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(202, 182)) == 0) {
        oriRopeCutL_mes[0].func = actSt04lOriRopeCutLChk;
        act->mail = oriRopeCutL_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt04lSword(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(175, 156)) == 0) {
        sword_mes[0].func = actSt04lSwordChk;
        act->mail = sword_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt04lGondola(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(190, 171)) != 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(228, 224, 200, 199), 0, 200);

        gondola_mes[0].func = actSt04lGondolaChk;
        act->mail = gondola_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(228, 224, 200, 199), 0, 0);

        gondola2_mes[0].func = actSt04lGondolaChk;
        act->mail = gondola2_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

static void actSt04lMonyou01_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(192) == 0) {
        stage_SetAnimation(206, 0, 0);

        monyou01_mes[0].func = actSt04lMonyou01Chk;
        act->mail = monyou01_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    } else {
        stage_SetAnimation(206, 0, -1);
    }
}

static void actSt04lMonyou01_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(192) == 0) {
        stage_SetAnimation(205, 0, 0);

        monyou01_mes[0].func = actSt04lMonyou01Chk;
        act->mail = monyou01_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    } else {
        stage_SetAnimation(205, 0, -1);
    }
}

void actSt04lMonyou01(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lMonyou01_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lMonyou01_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(212) == 0) {
        stage_SetAnimation(PAL_JPNKORCN(234, 230), 0, 0);

        monyou01_mes[0].func = actSt04lMonyou01Chk;
        act->mail = monyou01_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN(234, 230), 0, -1);
        FinishHint(16);
    }
}

void actSt04lMonyou02(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(213, 193)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(235, 231, 207, 206), 0, 0);

        monyou02_mes[0].func = actSt04lMonyou02Chk;
        act->mail = monyou02_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(235, 231, 207, 206), 0, -1);
    }
}

void actSt04lMonyou03(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(214, 194)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(236, 232, 208, 207), 0, 0);

        monyou03_mes[0].func = actSt04lMonyou03Chk;
        act->mail = monyou03_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(236, 232, 208, 207), 0, -1);
    }
}

void actSt04lMonyou04(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(215, 195)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(237, 233, 209, 208), 0, 0);

        monyou04_mes[0].func = actSt04lMonyou04Chk;
        act->mail = monyou04_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(237, 233, 209, 208), 0, -1);
    }
}

void actSt04lMonyou05(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(216, 196)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(238, 234, 210, 209), 0, 0);

        monyou05_mes[0].func = actSt04lMonyou05Chk;
        act->mail = monyou05_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(238, 234, 210, 209), 0, -1);
    }
}

void actSt04lMonyou06(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(217, 197)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(239, 235, 211, 210), 0, 0);

        monyou06_mes[0].func = actSt04lMonyou06Chk;
        act->mail = monyou06_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(239, 235, 211, 210), 0, -1);
    }
}

void actSt04lMonyou07(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(218, 198)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(240, 236, 212, 211), 0, 0);

        monyou07_mes[0].func = actSt04lMonyou07Chk;
        act->mail = monyou07_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(240, 236, 212, 211), 0, -1);
    }
}

void actSt04lCrest01XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(174, 155)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(204, 200, 182, 181), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(204, 200, 182, 181), 0, -1);
    }
}

void actSt04lCrest02XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(175, 156)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(205, 201, 183, 182), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(205, 201, 183, 182), 0, -1);
    }
}

void actSt04lCrest03XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(176, 157)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(206, 202, 184, 183), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(206, 202, 184, 183), 0, -1);
    }
}

void actSt04lC2BallXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(178, 159)) == 0) {
        scpSearchGobj(PAL_NTSC_AUG6JUN26(1118, 1030, 1027))->active = 0;
        scpSearchGobj(PAL_NTSC_AUG6JUN26(1119, 1031, 1028))->active = 0;

        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(208, 204, 186, 185), 0, 0);
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(208, 204, 186, 185), 0, -1);
    }
}

void actSt04lC3BallXL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(179, 160)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(209, 205, 187, 186), 0, 0);

        scpSearchGobj(PAL_NTSC_AUG6JUN26(1283, 1187, 1183))->active = 0;
        scpSearchGobj(PAL_NTSC_AUG6JUN26(1284, 1188, 1184))->active = 0;
    } else {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(209, 205, 187, 186), 0, -1);
    }
}

void actSt04lTorch1_1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(204, 184)) == 0) {
        torch1_1_mes[0].func = actSt04lTorch1_1Chk;
        act->mail = torch1_1_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        if (ICO_NTSC) {
            scpTorchLightOn(1026);
        } else if (ICO_AUG6JUN26) {
            scpTorchLightOn(1023);
        } else {
            scpTorchLightOn(1114);
            stage_SetAnimation(PAL_JPNKORCN(210, 206), 0, -1);
        }
    }
}

void actSt04lTorch1_2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(205, 185)) == 0) {
        torch1_2_mes[0].func = actSt04lTorch1_2Chk;
        act->mail = torch1_2_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        if (ICO_NTSC) {
            scpTorchLightOn(1027);
        } else if (ICO_AUG6JUN26) {
            scpTorchLightOn(1024);
        } else {
            scpTorchLightOn(1115);
            stage_SetAnimation(PAL_JPNKORCN(211, 207), 0, -1);
        }
    }
}

static void actSt04lTorch2_1_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(186) != 0 || gflagChk(188) != 0) {
        scpTorchLightOn(1108);
    } else {
        torch2_1_mes[0].func = actSt04lTorch2_1Chk;
        act->mail = torch2_1_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actSt04lTorch2_1_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(186) != 0 || gflagChk(188) != 0) {
        scpTorchLightOn(1105);
    } else {
        torch2_1_mes[0].func = actSt04lTorch2_1Chk;
        act->mail = torch2_1_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt04lTorch2_1(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lTorch2_1_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lTorch2_1_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(206) != 0 || gflagChk(208) != 0) {
        scpTorchLightOn(1202);
        stage_SetAnimation(PAL_JPNKORCN(212, 208), 0, -1);
    } else {
        torch2_1_mes[0].func = actSt04lTorch2_1Chk;
        act->mail = torch2_1_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static void actSt04lTorch2_2_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(187) != 0 || gflagChk(189) != 0) {
        scpTorchLightOn(1109);
    } else {
        torch2_2_mes[0].func = actSt04lTorch2_2Chk;
        act->mail = torch2_2_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actSt04lTorch2_2_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(187) != 0 || gflagChk(189) != 0) {
        scpTorchLightOn(1106);
    } else {
        torch2_2_mes[0].func = actSt04lTorch2_2Chk;
        act->mail = torch2_2_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt04lTorch2_2(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lTorch2_2_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lTorch2_2_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(207) != 0 || gflagChk(209) != 0) {
        scpTorchLightOn(1203);
        stage_SetAnimation(PAL_JPNKORCN(213, 209), 0, -1);
    } else {
        torch2_2_mes[0].func = actSt04lTorch2_2Chk;
        act->mail = torch2_2_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04lTorch3_1(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(210, 190)) == 0) {
        torch3_1_mes[0].func = actSt04lTorch3_1Chk;
        act->mail = torch3_1_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        if (ICO_NTSC) {
            scpTorchLightOn(1110);
        } else if (ICO_AUG6JUN26) {
            scpTorchLightOn(1107);
        } else {
            scpTorchLightOn(1204);
            stage_SetAnimation(PAL_JPNKORCN(214, 210), 0, -1);
        }
    }
}

void actSt04lTorch3_2(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(211, 191)) == 0) {
        torch3_2_mes[0].func = actSt04lTorch3_2Chk;
        act->mail = torch3_2_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        if (ICO_NTSC) {
            scpTorchLightOn(1111);
        } else if (ICO_AUG6JUN26) {
            scpTorchLightOn(1108);
        } else {
            scpTorchLightOn(1205);
            stage_SetAnimation(PAL_JPNKORCN(215, 211), 0, -1);
        }
    }
}

static void actSt04lTorch2_1XL_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(186) != 0 || gflagChk(188) != 0) {
        scpTorchLightOn(1030);
    } else {
        torch2_1XL_mes[0].func = actSt04lTorch2_1XLChk;
        act->mail = torch2_1XL_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actSt04lTorch2_1XL_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(186) != 0 || gflagChk(188) != 0) {
        scpTorchLightOn(1027);
    } else {
        torch2_1XL_mes[0].func = actSt04lTorch2_1XLChk;
        act->mail = torch2_1XL_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt04lTorch2_1XL(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lTorch2_1XL_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lTorch2_1XL_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(206) != 0 || gflagChk(208) != 0) {
        scpTorchLightOn(1118);
        stage_SetAnimation(PAL_JPNKORCN(212, 208), 0, -1);
    } else {
        torch2_1XL_mes[0].func = actSt04lTorch2_1XLChk;
        act->mail = torch2_1XL_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

static void actSt04lTorch2_2XL_ntsc(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(187) != 0 || gflagChk(189) != 0) {
        scpTorchLightOn(1031);
    } else {
        torch2_2XL_mes[0].func = actSt04lTorch2_2XLChk;
        act->mail = torch2_2XL_mes;
        ACTSendMailCorrect(self, 397);
        _ACTWait(0);
    }
}

static void actSt04lTorch2_2XL_aug6jun26(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(187) != 0 || gflagChk(189) != 0) {
        scpTorchLightOn(1028);
    } else {
        torch2_2XL_mes[0].func = actSt04lTorch2_2XLChk;
        act->mail = torch2_2XL_mes;
        ACTSendMailCorrect(self, 393);
        _ACTWait(0);
    }
}

void actSt04lTorch2_2XL(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lTorch2_2XL_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lTorch2_2XL_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(207) != 0 || gflagChk(209) != 0) {
        scpTorchLightOn(1119);
        stage_SetAnimation(PAL_JPNKORCN(213, 209), 0, -1);
    } else {
        torch2_2XL_mes[0].func = actSt04lTorch2_2XLChk;
        act->mail = torch2_2XL_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actSt04lTorch3_1XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(210, 190)) != 0) {
        scpTorchLightOn(PAL_NTSC_AUG6JUN26(1283, 1187, 1183));
    }
}

void actSt04lTorch3_2XL(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(211, 191)) != 0) {
        scpTorchLightOn(PAL_NTSC_AUG6JUN26(1284, 1188, 1184));
    }
}

void actSt04cDoorInit(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(162, 145)) != 0 && gflagChk(PAL_NTSCAUG6JUN26(174, 155)) == 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(253, 249, 225, 224), 0, -1);
    }

    if (gflagChk(PAL_NTSCAUG6JUN26(162, 145)) == 0 || gflagChk(PAL_NTSCAUG6JUN26(174, 155)) != 0) {
        stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(253, 249, 225, 224), 0, 0);
    }
}

static void actSt04dEnemy1_aug6jun26(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    Generator_Mask(scpSearchGobj(1095));

    while (gflagChk(200) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_Call(scpSearchGobj(1095));
}

static void actSt04dEnemy1_ntsc(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);
    Generator_Mask(scpSearchGobj(1098));

    while (gflagChk(200) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
    Generator_Call(scpSearchGobj(1098));
}

void actSt04dEnemy1(GObj *volatile self)
{
    if (ICO_AUG6JUN26) {
        actSt04dEnemy1_aug6jun26(self);
        return;
    }
    if (ICO_NTSC) {
        actSt04dEnemy1_ntsc(self);
        return;
    }
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6JUN26(220, 200)) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt04dEnemy2(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6JUN26(220, 200)) == 0) {
        _ACTWait(1);
    }

    Generator_MaskOff(self);
    Generator_Call(self);
    _ACTWait(60);
    Generator_Call(self);
}

void actSt04dEnemy3(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
    Generator_Mask(self);

    while (gflagChk(PAL_NTSCAUG6JUN26(220, 200)) == 0) {
        _ACTWait(1);
    }

    Generator_Call(self);
}

void actSt04eSolarBeam(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(PAL_NTSCAUG6JUN26(224, 204)) != 0) {
        if (boyGObj != 0) {
            scpPlayMot(boyGObj, 0);

            if (!ICO_AUG6JUN26) {
                if (ACTGame_NoWeapon(boyGObj) == 0) {
                    LightTorchOffOfWeapon(GetBoyWeaponGObj());
                }
            }
        }

        gflagOn(PAL_NTSC_AUG6JUN26(389, 356, 355));

        scpSeEnvMasterVolRate = 0.0f;
        scpFadeOut(255.0f, 0, 0, 0);

        st04eSolarBeam_mes[0].func = actSt04eSolarBeamChk;
        act->mail = st04eSolarBeam_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt04lC1BallMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    if (ICO_AUG6JUN26) {
        scpBoyControlReadDisable = 0;
    }

    sub->mainMail = c1BallMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt04lC1BallSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    c1BallSwitch_mes[0].func = actSt04lC1BallTurn;
    sub->mail = c1BallSwitch_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

static void actSt04lC1BallTurn_ntsc(GObj *volatile self)
{
    lt_switch_layout(51);
    gflagOn(158);
    scpSleepEnemyAll();

    _ACTWait(60);

    scpAdpcmPlayRequestFunc(80, &ball1_4l, 1, 1, 1);

    while (ball1_4l == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(185, 1, 0);

    while (stage_CheckAnimationFinish(185) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    scpSearchGobj(1026)->active = 1;
    scpSearchGobj(1027)->active = 1;

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 6);

    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static void actSt04lC1BallTurn_aug6jun26(GObj *volatile self)
{
    lt_switch_layout(51);
    gflagOn(158);
    scpSleepEnemyAll();

    _ACTWait(60);

    scpAdpcmPlayRequestFunc(80, &ball1_4l, 1, 1, 1);

    while (ball1_4l == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(184, 1, 0);

    while (stage_CheckAnimationFinish(184) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    scpSearchGobj(1023)->active = 1;
    scpSearchGobj(1024)->active = 1;

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 6);

    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actSt04lC1BallTurn(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lC1BallTurn_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lC1BallTurn_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    turnFlag = 177;
    turnAnim = PAL_JPNKORCN(207, 203);
    turnGobj1 = 1114;
    turnGobj2 = 1115;

    c1BallTurn_mes[0].func = actSt04lBallTurnCommon;
    act->mail = c1BallTurn_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04lC2BallMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    if (ICO_AUG6JUN26) {
        scpBoyControlReadDisable = 0;
    }

    sub->mainMail = c2BallMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt04lC2BallSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    c2BallSwitch_mes[0].func = actSt04lC2BallTurn;
    sub->mail = c2BallSwitch_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

static void actSt04lC2BallTurn_ntsc(GObj *volatile self)
{
    lt_switch_layout(51);
    gflagOn(159);
    scpSleepEnemyAll();

    _ACTWait(60);

    scpAdpcmPlayRequestFunc(80, &ball2_4l, 1, 1, 1);

    while (ball2_4l == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(186, 1, 0);

    while (stage_CheckAnimationFinish(186) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    scpSearchGobj(1108)->active = 1;
    scpSearchGobj(1109)->active = 1;

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 6);

    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static void actSt04lC2BallTurn_aug6jun26(GObj *volatile self)
{
    lt_switch_layout(51);
    gflagOn(159);
    scpSleepEnemyAll();

    _ACTWait(60);

    scpAdpcmPlayRequestFunc(80, &ball2_4l, 1, 1, 1);

    while (ball2_4l == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(185, 1, 0);

    while (stage_CheckAnimationFinish(185) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    scpSearchGobj(1105)->active = 1;
    scpSearchGobj(1106)->active = 1;

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 6);

    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actSt04lC2BallTurn(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lC2BallTurn_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lC2BallTurn_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    turnFlag = 178;
    turnAnim = PAL_JPNKORCN(208, 204);
    turnGobj1 = 1202;
    turnGobj2 = 1203;

    c2BallTurn_mes[0].func = actSt04lBallTurnCommon;
    act->mail = c2BallTurn_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04lC3BallMain(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);
    if (ICO_AUG6JUN26) {
        scpBoyControlReadDisable = 0;
    }

    sub->mainMail = c3BallMain_mes;
    while (1) {
        _ACTWait(1);
    }
}

void actSt04lC3BallSwitch(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    scpBoyControlReadDisable = 1;

    sub->mainMail = 0;

    c3BallSwitch_mes[0].func = actSt04lC3BallTurn;
    sub->mail = c3BallSwitch_mes;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

static void actSt04lC3BallTurn_ntsc(GObj *volatile self)
{
    lt_switch_layout(51);
    scpSleepEnemyAll();
    gflagOn(160);

    _ACTWait(60);

    scpAdpcmPlayRequestFunc(80, &ball3_4l, 1, 1, 1);

    while (ball3_4l == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(187, 1, 0);

    while (stage_CheckAnimationFinish(187) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    scpSearchGobj(1110)->active = 1;
    scpSearchGobj(1111)->active = 1;

    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static void actSt04lC3BallTurn_aug6jun26(GObj *volatile self)
{
    lt_switch_layout(51);
    scpSleepEnemyAll();
    gflagOn(160);

    _ACTWait(60);

    scpAdpcmPlayRequestFunc(80, &ball3_4l, 1, 1, 1);

    while (ball3_4l == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(186, 1, 0);

    while (stage_CheckAnimationFinish(186) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);

    scpSearchGobj(1107)->active = 1;
    scpSearchGobj(1108)->active = 1;

    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actSt04lC3BallTurn(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lC3BallTurn_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lC3BallTurn_aug6jun26(self);
        return;
    }
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    turnFlag = 179;
    turnAnim = PAL_JPNKORCN(209, 205);
    turnGobj1 = 1204;
    turnGobj2 = 1205;

    c3BallTurn_mes[0].func = actSt04lBallTurnCommon;
    act->mail = c3BallTurn_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

static void actSt04eSolarBeamChkSub(GObj *volatile self)
{
    stage_SetAnimation(PAL_JPNKORCN(292, 288), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN(292, 288)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    demoEnd = 1;
    _ACTWait(0);
}

void actSt04lRope1Event(int x)
{
    volatile int local = x;
}

void actSt04lRope2Event(int x)
{
    volatile int local = x;
}

void actSt04lBrg1Event(int x)
{
    volatile int local = x;
}

static void actSt04lBrg1Chk_ntsc(GObj *volatile self)
{
    while (gflagChk(165) == 0 || gflagChk(166) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();
    gflagOn(163);

    _ACTWait(60);

    scpAdpcmPlayRequestFunc(84, &st04d_hasi, 1, 1, 1);

    while (st04d_hasi == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(196, 1, 0);

    while (stage_CheckAnimationFrame(196, 75, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFinish(196) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static void actSt04lBrg1Chk_aug6jun26(GObj *volatile self)
{
    while (gflagChk(165) == 0 || gflagChk(166) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();
    gflagOn(163);

    _ACTWait(60);

    scpAdpcmPlayRequestFunc(84, &st04d_hasi, 1, 1, 1);

    while (st04d_hasi == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(195, 1, 0);

    while (stage_CheckAnimationFrame(195, 75, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFinish(195) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actSt04lBrg1Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lBrg1Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lBrg1Chk_aug6jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    while (gflagChk(184) == 0 || gflagChk(185) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();
    gflagOn(182);

    sekizo_4r = PAL_JPNKORCN(224, 220);

    brg1Chk_mes[0].func = actSt04rBrgCommon;
    sub->mail = brg1Chk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04lRope3Event(int x)
{
    volatile int local = x;
}

void actSt04lRope4Event(int x)
{
    volatile int local = x;
}

void actSt04lBrg2Event(int x)
{
    volatile int local = x;
}

static void actSt04lBrg2Chk_ntsc(GObj *volatile self)
{
    while (gflagChk(167) == 0 || gflagChk(168) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();
    gflagOn(164);

    _ACTWait(60);

    scpAdpcmPlayRequestFunc(84, &st04d_hasi, 1, 1, 1);

    while (st04d_hasi == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(197, 1, 0);

    while (stage_CheckAnimationFrame(197, 75, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFinish(197) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

static void actSt04lBrg2Chk_aug6jun26(GObj *volatile self)
{
    while (gflagChk(167) == 0 || gflagChk(168) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();
    gflagOn(164);

    _ACTWait(60);

    scpAdpcmPlayRequestFunc(84, &st04d_hasi, 1, 1, 1);

    while (st04d_hasi == 0) {
        _ACTWait(1);
    }

    stage_SetAnimation(196, 1, 0);

    while (stage_CheckAnimationFrame(196, 75, 0) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    iosPadActRequest(boyPad, 16);

    while (stage_CheckAnimationFinish(196) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    scpWakeupEnemyAll();
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
}

void actSt04lBrg2Chk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actSt04lBrg2Chk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actSt04lBrg2Chk_aug6jun26(self);
        return;
    }
    Act *sub = GOBJ_ACT(self);

    while (gflagChk(186) == 0 || gflagChk(187) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpSleepEnemyAll();
    gflagOn(183);
    WakeupHint(16);

    sekizo_4r = PAL_JPNKORCN(225, 221);

    brg2Chk_mes[0].func = actSt04rBrgCommon;
    sub->mail = brg2Chk_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actSt04lBrg1WayChk(GObj *volatile self)
{
    while (gflagChk(PAL_NTSCAUG6JUN26(182, 163)) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(3, 1);
}

void actSt04lBrg2WayChk(GObj *volatile self)
{
    while (gflagChk(PAL_NTSCAUG6JUN26(182, 163)) == 0 ||
           gflagChk(PAL_NTSCAUG6JUN26(183, 164)) == 0) {
        _ACTWait(1);
    }

    SetWayGroupActive(4, 1);
}

void actSt04lTuriEvent(int x)
{
    volatile int local = x;
}

void actSt04lTuriChk(GObj *volatile self)
{
    if (girlGObj == 0) {
        _ACTWait(0);
    }

    while (scpTriggerBall(self, girlGObj, 200.0f) == 0) {
        _ACTWait(1);
    }

    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
    gflagOn(PAL_NTSCAUG6JUN26(189, 170));
    _ACTWait(10);

    stage_SetAnimation(PAL_JPNKORCN_NTSC_AUG6JUN26(230, 226, 202, 201), 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKORCN_NTSC_AUG6JUN26(230, 226, 202, 201)) == 0) {
        _ACTWait(1);
    }

    _ACTWait(1);
    lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(54, 51, 50));
}

void actSt04lGondolaCharaChk(GObj *volatile self)
{
    Act *sub = GOBJ_ACT(self);

    while (scpTriggerFloorAttr(boyGObj, 0xA000000)) {
        if (!ICO_NTSCAUG6JUN26) {
            if (gflagChk(194) && scpTriggerFloorAttr(girlGObj, 0xA000000)) {
                break;
            }
        }
        _ACTWait(1);
    }

    gondola_chara[0].func = actSt04lGondolaChk;
    sub->mail = gondola_chara;
    ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
    _ACTWait(0);
}

void actSt04lMonyou01Event(int x)
{
    volatile int local = x;
}

void actSt04lMonyou02Event(int x)
{
    volatile int local = x;
}

void actSt04lMonyou03Event(int x)
{
    volatile int local = x;
}

void actSt04lMonyou04Event(int x)
{
    volatile int local = x;
}

void actSt04lMonyou05Event(int x)
{
    volatile int local = x;
}

void actSt04lMonyou06Event(int x)
{
    volatile int local = x;
}

void actSt04lMonyou07Event(int x)
{
    volatile int local = x;
}

void actSt04lOriEvent(int x)
{
    volatile int local = x;
}

void actSt04lOri2Event(int x)
{
    volatile int local = x;
}

void actSt04lSwordChk(GObj *volatile self)
{
    scpSearchGobj(PAL_NTSC_AUG6JUN26(1122, 1034, 1031))->active = 0;
}

void actSt04lTorch1_1Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(PAL_NTSC_AUG6JUN26(1114, 1026, 1023)) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSCAUG6JUN26) {
        gflagOn(184);
    } else {
        gflagOn(204);
        stage_SetAnimation(PAL_JPNKORCN(210, 206), 1, 0);
    }
}

void actSt04lTorch1_2Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(PAL_NTSC_AUG6JUN26(1115, 1027, 1024)) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSCAUG6JUN26) {
        gflagOn(185);
    } else {
        gflagOn(205);
        stage_SetAnimation(PAL_JPNKORCN(211, 207), 1, 0);
    }
}

void actSt04lTorch2_1Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(PAL_NTSC_AUG6JUN26(1202, 1108, 1105)) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSCAUG6JUN26) {
        gflagOn(186);
    } else {
        gflagOn(206);
        stage_SetAnimation(PAL_JPNKORCN(212, 208), 1, 0);
    }
}

void actSt04lTorch2_2Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(PAL_NTSC_AUG6JUN26(1203, 1109, 1106)) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSCAUG6JUN26) {
        gflagOn(187);
    } else {
        gflagOn(207);
        stage_SetAnimation(PAL_JPNKORCN(213, 209), 1, 0);
    }
}

void actSt04lTorch3_1Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(PAL_NTSC_AUG6JUN26(1204, 1110, 1107)) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSCAUG6JUN26) {
        gflagOn(190);
    } else {
        gflagOn(210);
        stage_SetAnimation(PAL_JPNKORCN(214, 210), 1, 0);
    }
}

void actSt04lTorch3_2Chk(GObj *volatile self)
{
    while (scpIsTorchLightOn(PAL_NTSC_AUG6JUN26(1205, 1111, 1108)) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSCAUG6JUN26) {
        gflagOn(191);
    } else {
        gflagOn(211);
        stage_SetAnimation(PAL_JPNKORCN(215, 211), 1, 0);
    }
}

void actSt04lTorch2_1XLChk(GObj *volatile self)
{
    while (scpIsTorchLightOn(PAL_NTSC_AUG6JUN26(1118, 1030, 1027)) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSCAUG6JUN26) {
        gflagOn(188);
    } else {
        gflagOn(208);
        stage_SetAnimation(PAL_JPNKORCN(212, 208), 1, 0);
    }
}

void actSt04lTorch2_2XLChk(GObj *volatile self)
{
    while (scpIsTorchLightOn(PAL_NTSC_AUG6JUN26(1119, 1031, 1028)) == 0) {
        _ACTWait(1);
    }

    if (ICO_NTSCAUG6JUN26) {
        gflagOn(189);
    } else {
        gflagOn(209);
        stage_SetAnimation(PAL_JPNKORCN(213, 209), 1, 0);
    }
}
