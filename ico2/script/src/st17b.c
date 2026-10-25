#include "st17b.h"
#include "layout_texture.h"
#include "act.h"
#include "commonact.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "StageManager.h"
#include "script.h"
#include "typedef.h"
#include "main.h"

static ActMail check_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail check_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail check_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define check_mes PAL_NTSC_AUG6(check_mes_pal, check_mes_ntsc, check_mes_aug6)

static ActMail intro_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail intro_mes_aug6[2] = {{393}, {392}}; /* derived name */

#define intro_mes PAL_AUG6(intro_mes_ntsc, intro_mes_aug6)

/* .sdata: the lightning stream handle. */
struct SqEntry *lightning2 = 0;

void actSt17bTest(GObj *volatile self)
{
    GObj *x = self;

    actInitialize(self);
    _ACTWait(1);
}

void actSt17bCheck(GObj *volatile self)
{
    GObj *x = self;
    Act *act = (Act *)actInitialize(self);

    _ACTWait(1);
    ScpCallCameraSetTarget(14990.0f, 7074.0f, -4694.0f);
    scpAdpcmPlayRequestFunc(PAL_NTSCAUG6(94, 92), &lightning2, 1, 0, 1);

    if (gflagChk(PAL_NTSCAUG6(36, 33)) == 0) {
        check_mes[0].func = actSt17bCheckChk;
        act->mail = check_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6(430, 397, 393));
        _ACTWait(0);
    }
}

void actSt17bIntro(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(34) == 0) {
        intro_mes[0].func = actSt17bIntroChk;
        act->mail = intro_mes;
        ACTSendMailCorrect(self, PAL_AUG6(397, 393));
        _ACTWait(0);
    }
}

void actSt17bCheckChk(GObj *volatile self)
{
    _ACTWait(1);
    CheckPoint();
    gflagOn(PAL_NTSCAUG6(34, 32));
}

void actSt17bIntroChk(GObj *volatile self)
{
    while (scpTriggerFloorAttr(boyGObj, 0x1000000) == 0) {
        _ACTWait(1);
    }
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    gflagOn(34);
    stage_SetAnimation(PAL_AUG6(116, 115), 1, 0);
    while (stage_CheckAnimationFinish(PAL_AUG6(116, 115)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    lt_switch_layout(50);
    scpBoyControlReadDisable = 0;
}
