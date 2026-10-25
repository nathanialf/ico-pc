#include "deja.h"
#include "StageManager.h"
#include "layout_texture.h"
#include "act.h"
#include "commonact.h"
#include "fightSound.h"
#include "gflag.h"
#include "script.h"
#include "Shadow.h"
#include "StageAnimation.h"
#include <libvu0.h>
#include "typedef.h"
#include "main.h"
#include "ico_gamestate.h" /* port: achievement signals */

/* actDeja installs actDejaChk as the actor's next mail handler. */

static ActMail _mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail _mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail _mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define _mes PAL_NTSC_AUG6JUN26(_mes_pal, _mes_ntsc, _mes_aug6jun26)

static ActMail after_mes_pal[2] = {{430}, {429}}; /* derived name */

static ActMail after_mes_ntsc[2] = {{397}, {396}}; /* derived name */

static ActMail after_mes_aug6jun26[2] = {{393}, {392}}; /* derived name */

#define after_mes PAL_NTSC_AUG6JUN26(after_mes_pal, after_mes_ntsc, after_mes_aug6jun26)

/* .sbss: the demo's own end flag, raised by the handler the wait loop below
   is spinning for. */
static int demoEnd;

void actDejaDemo(GObj *volatile self);
void actDejaAfterChk(GObj *volatile self);

/* .sdata: the scene's stream handle */
struct SqEntry *deja = 0;

static const Vec16 afterChkPos = {{-1000.0f, 0.0f, -2200.0f, 1.0f}}; /* derived name */

inline void actEnemySleep(GObj *volatile self)
{
    while (1) {
        scpSleepEnemyAll();
        _ACTWait(1);
    }
}

inline void actDeja(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (gflagChk(6) == 0) {
        lt_switch_layout(PAL_JPNKORCN_NTSCAUG6JUN26(55, 52, 51));
        scpBoyControlReadDisable = 1;
        scpFadeOut(255.0f, 0, 0, 0);
        stage_SetAnimation(PAL_JPNKORCN_NTSCAUG6JUN26(72, 68, 59), 0, 0);
        _mes[0].func = actDejaChk;
        act->mail = _mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    }
}

void actDejaChk(GObj *volatile self)
{
    gflagOn(6);
    scpSearchGobj(PAL_NTSC_AUG6JUN26(2548, 2340, 2311))->active = 0;
    scpSearchGobj(PAL_NTSC_AUG6JUN26(2549, 2341, 2312))->active = 0;
    actCreateSubThread(actEnemySleep, 21);
    scpAdpcmPlayRequestFunc(12, &deja, 0, 1, 1);
    while (deja == 0) {
        _ACTWait(1);
    }
    scpFadeIn(6.0f);
    actCreateSubThread(actDejaDemo, 21);
    fightSoundProcessRequestPause();
    while (fightSoundPlayChk() != 0) {
        _ACTWait(1);
    }
    demoEnd = 0;
    while (demoEnd == 0 && ((pad[0].flags & 0x800) == 0 || scpAdpcmPlayRequestNum() != 0)) {
        _ACTWait(1);
    }
    ico_gs_signal(ICO_GS_EV_DEMO_END, 10 + (demoEnd == 0));
    if (demoEnd == 0 && deja != 0) {
        if (ICO_NTSC) {
            shadow_DispCancel(68, 0);
            scpAdpcmFadeClose(deja, 128);
        } else if (ICO_AUG6JUN26) {
            shadow_DispCancel(67, 0);
            scpAdpcmFadeClose(deja, 128);
        } else {
            shadow_DispCancel(74, 0);
            scpAdpcmFadeCloseFunc(&deja, 128);
        }
        deja = 0;
    }
    RequestStageChange(1, boyGObj, 0, 0.025f, 1.0f);
}

static void actDejaDemo_ntsc(GObj *volatile self)
{
    stgmgrNextStagePreLoadForceStageSet(exitData[stageData[stage_no].ent[0]].nextStage);
    scpPlayStart(boyGObj);
    stage_SetAnimation(537, 1, 0);
    scpPlayMot(boyGObj, 292);
    while (stage_ContinueAnimation(537, 538) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 293);
    while (stage_ContinueAnimation(538, 539) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 294);
    while (stage_ContinueAnimation(539, 540) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 295);
    while (stage_ContinueAnimation(540, 541) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 296);
    _ACTWait(1);
    stage_SetAnimation(548, 1, 0);
    while (stage_CheckAnimationFrame(541, 120, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpSearchGobj(51)->active = 0;
    while (stage_ContinueAnimation(541, 542) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    stage_SetAnimation(548, -1, -2);
    stage_SetAnimation(549, 1, 0);
    while (stage_ContinueAnimation(542, 543) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(scpSearchGobj(2340), 679);
    scpSearchGobj(2340)->active = 1;
    _ACTWait(1);
    stage_SetAnimation(549, -1, -2);
    stage_SetAnimation(550, 1, 0);
    shadow_DispCancel(68, 1);
    while (stage_ContinueAnimation(543, 544) == 0) {
        _ACTWait(1);
    }
    scpSearchGobj(51)->active = 1;
    scpPlayMot(boyGObj, 297);
    _ACTWait(1);
    stage_SetAnimation(550, -1, -2);
    while (stage_ContinueAnimation(544, 545) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 298);
    scpPlayMot(scpSearchGobj(2340), 680);
    _ACTWait(1);
    stage_SetAnimation(551, 1, 0);
    while (stage_ContinueAnimation(545, 546) == 0) {
        _ACTWait(1);
    }
    scpSearchGobj(2341)->active = 1;
    scpPlayMot(boyGObj, 299);
    scpPlayStart(scpSearchGobj(2341));
    scpPlayMot(scpSearchGobj(2341), 913);
    _ACTWait(1);
    stage_SetAnimation(551, -1, -2);
    stage_SetAnimation(552, 1, 0);
    while (stage_ContinueAnimation(546, 547) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 300);
    scpPlayMot(scpSearchGobj(2341), 914);
    shadow_DispCancel(68, 0);
    while (stage_CheckAnimationFrame(547, 125, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    demoEnd = 1;
}

static void actDejaDemo_aug6jun26(GObj *volatile self)
{
    stgmgrNextStagePreLoadForceStageSet(exitData[stageData[stage_no].ent[0]].nextStage);
    scpPlayStart(boyGObj);
    stage_SetAnimation(536, 1, 0);
    scpPlayMot(boyGObj, 292);
    while (stage_ContinueAnimation(536, 537) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 293);
    while (stage_ContinueAnimation(537, 538) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 294);
    while (stage_ContinueAnimation(538, 539) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 295);
    while (stage_ContinueAnimation(539, 540) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 296);
    _ACTWait(1);
    stage_SetAnimation(547, 1, 0);
    while (stage_CheckAnimationFrame(540, 120, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpSearchGobj(50)->active = 0;
    while (stage_ContinueAnimation(540, 541) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    stage_SetAnimation(547, -1, -2);
    stage_SetAnimation(548, 1, 0);
    while (stage_ContinueAnimation(541, 542) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(scpSearchGobj(2311), 681);
    scpSearchGobj(2311)->active = 1;
    _ACTWait(1);
    stage_SetAnimation(548, -1, -2);
    stage_SetAnimation(549, 1, 0);
    shadow_DispCancel(67, 1);
    while (stage_ContinueAnimation(542, 543) == 0) {
        _ACTWait(1);
    }
    scpSearchGobj(50)->active = 1;
    scpPlayMot(boyGObj, 297);
    _ACTWait(1);
    stage_SetAnimation(549, -1, -2);
    while (stage_ContinueAnimation(543, 544) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 298);
    scpPlayMot(scpSearchGobj(2311), 682);
    _ACTWait(1);
    stage_SetAnimation(550, 1, 0);
    while (stage_ContinueAnimation(544, 545) == 0) {
        _ACTWait(1);
    }
    scpSearchGobj(2312)->active = 1;
    scpPlayMot(boyGObj, 299);
    scpPlayStart(scpSearchGobj(2312));
    scpPlayMot(scpSearchGobj(2312), 915);
    _ACTWait(1);
    stage_SetAnimation(550, -1, -2);
    stage_SetAnimation(551, 1, 0);
    while (stage_ContinueAnimation(545, 546) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 300);
    scpPlayMot(scpSearchGobj(2312), 916);
    shadow_DispCancel(67, 0);
    while (stage_CheckAnimationFrame(546, 125, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    demoEnd = 1;
}

void actDejaDemo(GObj *volatile self)
{
    if (ICO_NTSC) {
        actDejaDemo_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actDejaDemo_aug6jun26(self);
        return;
    }
    stgmgrNextStagePreLoadForceStageSet(exitData[stageData[stage_no].ent[0]].nextStage);
    scpPlayStart(boyGObj);
    stage_SetAnimation(PAL_JPNKORCN(608, 604), 1, 0);
    scpPlayMot(boyGObj, 298);
    while (stage_ContinueAnimation(PAL_JPNKORCN(608, 604), PAL_JPNKORCN(609, 605)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 299);
    while (stage_ContinueAnimation(PAL_JPNKORCN(609, 605), PAL_JPNKORCN(610, 606)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 300);
    while (stage_ContinueAnimation(PAL_JPNKORCN(610, 606), PAL_JPNKORCN(611, 607)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 301);
    while (stage_ContinueAnimation(PAL_JPNKORCN(611, 607), PAL_JPNKORCN(612, 608)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 302);
    _ACTWait(1);
    stage_SetAnimation(PAL_JPNKORCN(619, 615), 1, 0);
    while (stage_CheckAnimationFrame(PAL_JPNKORCN(612, 608), 120, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpSearchGobj(54)->active = 0;
    while (stage_ContinueAnimation(PAL_JPNKORCN(612, 608), PAL_JPNKORCN(613, 609)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    stage_SetAnimation(PAL_JPNKORCN(619, 615), -1, -2);
    stage_SetAnimation(PAL_JPNKORCN(620, 616), 1, 0);
    while (stage_ContinueAnimation(PAL_JPNKORCN(613, 609), PAL_JPNKORCN(614, 610)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(scpSearchGobj(2548), 724);
    scpSearchGobj(2548)->active = 1;
    _ACTWait(1);
    stage_SetAnimation(PAL_JPNKORCN(620, 616), -1, -2);
    stage_SetAnimation(PAL_JPNKORCN(621, 617), 1, 0);
    shadow_DispCancel(74, 1);
    while (stage_ContinueAnimation(PAL_JPNKORCN(614, 610), PAL_JPNKORCN(615, 611)) == 0) {
        _ACTWait(1);
    }
    scpSearchGobj(54)->active = 1;
    scpPlayMot(boyGObj, 303);
    _ACTWait(1);
    stage_SetAnimation(PAL_JPNKORCN(621, 617), -1, -2);
    while (stage_ContinueAnimation(PAL_JPNKORCN(615, 611), PAL_JPNKORCN(616, 612)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 304);
    scpPlayMot(scpSearchGobj(2548), 725);
    _ACTWait(1);
    stage_SetAnimation(PAL_JPNKORCN(622, 618), 1, 0);
    while (stage_ContinueAnimation(PAL_JPNKORCN(616, 612), PAL_JPNKORCN(617, 613)) == 0) {
        _ACTWait(1);
    }
    scpSearchGobj(2549)->active = 1;
    scpPlayMot(boyGObj, 305);
    scpPlayStart(scpSearchGobj(2549));
    scpPlayMot(scpSearchGobj(2549), 962);
    _ACTWait(1);
    stage_SetAnimation(PAL_JPNKORCN(622, 618), -1, -2);
    stage_SetAnimation(PAL_JPNKORCN(623, 619), 1, 0);
    while (stage_ContinueAnimation(PAL_JPNKORCN(617, 613), PAL_JPNKORCN(618, 614)) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 306);
    scpPlayMot(scpSearchGobj(2549), 963);
    shadow_DispCancel(74, 0);
    while (stage_CheckAnimationFrame(PAL_JPNKORCN(618, 614), 125, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    demoEnd = 1;
}

void actDejaAfter(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    ScpCallCameraOff();
    if (gflagChk(7) == 0 && gflagChk(6) != 0) {
        stage_SetAnimation(PAL_NTSCAUG6JUN26(32, 31), 0, -1);
        stage_SetAnimation(PAL_NTSCAUG6JUN26(26, 25), 0, -1);
        stage_SetAnimation(PAL_NTSCAUG6JUN26(35, 34), 0, 0);
        stage_SetAnimation(PAL_NTSCAUG6JUN26(33, 32), 0, 0);
        stage_SetAnimation(PAL_NTSCAUG6JUN26(36, 35), 0, 0);
        stage_SetAnimation(PAL_NTSCAUG6JUN26(38, 37), 0, 0);
        after_mes[0].func = actDejaAfterChk;
        act->mail = after_mes;
        ACTSendMailCorrect(self, PAL_NTSC_AUG6JUN26(430, 397, 393));
        _ACTWait(0);
    } else {
        stage_SetAnimation(PAL_NTSCAUG6JUN26(32, 31), 0, -1);
        stage_SetAnimation(PAL_NTSCAUG6JUN26(26, 25), 0, -1);
        stage_SetAnimation(PAL_NTSCAUG6JUN26(35, 34), 0, 0);
        stage_SetAnimation(PAL_NTSCAUG6JUN26(33, 32), 0, 0);
        stage_SetAnimation(PAL_NTSCAUG6JUN26(36, 35), 0, 0);
        stage_SetAnimation(PAL_NTSCAUG6JUN26(38, 37), 0, 0);
    }
}

static void actDejaAfterChk_ntsc(GObj *volatile self)
{
    Vec16 target;
    Vec16 dir;

    CheckPoint();
    gflagOn(7);
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    stage_SetAnimation(553, 1, 0);
    scpPlayMot(boyGObj, 301);
    while (stage_CheckAnimationFinish(553) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpPlayMot(boyGObj, 0);
    target = afterChkPos;
    sceVu0SubVector(dir.f, target.f, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir.f);
    scpPlayEnd(boyGObj);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    _ACTWait(60);
    if (deja != 0) {
        scpAdpcmFadeClose(deja, 80);
    }
    deja = 0;
    _ACTWait(0);
}

static void actDejaAfterChk_aug6jun26(GObj *volatile self)
{
    Vec16 target;
    Vec16 dir;

    CheckPoint();
    gflagOn(7);
    lt_switch_layout(51);
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    stage_SetAnimation(552, 1, 0);
    scpPlayMot(boyGObj, 301);
    while (stage_CheckAnimationFinish(552) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpPlayMot(boyGObj, 0);
    target = afterChkPos;
    sceVu0SubVector(dir.f, target.f, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir.f);
    scpPlayEnd(boyGObj);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(50);
    _ACTWait(60);
    if (deja != 0) {
        scpAdpcmFadeClose(deja, 80);
    }
    deja = 0;
    _ACTWait(0);
}

void actDejaAfterChk(GObj *volatile self)
{
    if (ICO_NTSC) {
        actDejaAfterChk_ntsc(self);
        return;
    }
    if (ICO_AUG6JUN26) {
        actDejaAfterChk_aug6jun26(self);
        return;
    }
    Vec16 target;
    Vec16 dir;

    CheckPoint();
    gflagOn(7);
    lt_switch_layout(PAL_JPNKORCN(55, 52));
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    stage_SetAnimation(PAL_JPNKORCN(624, 620), 1, 0);
    scpPlayMot(boyGObj, 307);
    while (stage_CheckAnimationFinish(PAL_JPNKORCN(624, 620)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpPlayMot(boyGObj, 0);
    target = afterChkPos;
    sceVu0SubVector(dir.f, target.f, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir.f);
    scpPlayEnd(boyGObj);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(PAL_JPNKORCN(54, 51));
    _ACTWait(60);
    if (deja != 0) {
        scpAdpcmFadeCloseFunc(&deja, 80);
    }
    deja = 0;
    _ACTWait(0);
}
