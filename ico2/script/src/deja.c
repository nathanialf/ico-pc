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
#include "ico_gamestate.h" /* port: achievement signals, docs/port/ACHIEVEMENTS.md */

/* actDeja installs actDejaChk as the actor's next mail handler. */

static ActMail _mes[2] = {{430}, {429}}; /* derived name */

static ActMail after_mes[2] = {{430}, {429}}; /* derived name */

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
        lt_switch_layout(55);
        scpBoyControlReadDisable = 1;
        scpFadeOut(255.0f, 0, 0, 0);
        stage_SetAnimation(72, 0, 0);
        _mes[0].func = actDejaChk;
        act->mail = _mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    }
}

void actDejaChk(GObj *volatile self)
{
    gflagOn(6);
    scpSearchGobj(2548)->active = 0;
    scpSearchGobj(2549)->active = 0;
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
        shadow_DispCancel(74, 0);
        scpAdpcmFadeCloseFunc(&deja, 128);
        deja = 0;
    }
    RequestStageChange(1, boyGObj, 0, 0.025f, 1.0f);
}

void actDejaDemo(GObj *volatile self)
{
    stgmgrNextStagePreLoadForceStageSet(exitData[stageData[stage_no].ent[0]].nextStage);
    scpPlayStart(boyGObj);
    stage_SetAnimation(608, 1, 0);
    scpPlayMot(boyGObj, 298);
    while (stage_ContinueAnimation(608, 609) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 299);
    while (stage_ContinueAnimation(609, 610) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 300);
    while (stage_ContinueAnimation(610, 611) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 301);
    while (stage_ContinueAnimation(611, 612) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 302);
    _ACTWait(1);
    stage_SetAnimation(619, 1, 0);
    while (stage_CheckAnimationFrame(612, 120, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpSearchGobj(54)->active = 0;
    while (stage_ContinueAnimation(612, 613) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    stage_SetAnimation(619, -1, -2);
    stage_SetAnimation(620, 1, 0);
    while (stage_ContinueAnimation(613, 614) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(scpSearchGobj(2548), 724);
    scpSearchGobj(2548)->active = 1;
    _ACTWait(1);
    stage_SetAnimation(620, -1, -2);
    stage_SetAnimation(621, 1, 0);
    shadow_DispCancel(74, 1);
    while (stage_ContinueAnimation(614, 615) == 0) {
        _ACTWait(1);
    }
    scpSearchGobj(54)->active = 1;
    scpPlayMot(boyGObj, 303);
    _ACTWait(1);
    stage_SetAnimation(621, -1, -2);
    while (stage_ContinueAnimation(615, 616) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 304);
    scpPlayMot(scpSearchGobj(2548), 725);
    _ACTWait(1);
    stage_SetAnimation(622, 1, 0);
    while (stage_ContinueAnimation(616, 617) == 0) {
        _ACTWait(1);
    }
    scpSearchGobj(2549)->active = 1;
    scpPlayMot(boyGObj, 305);
    scpPlayStart(scpSearchGobj(2549));
    scpPlayMot(scpSearchGobj(2549), 962);
    _ACTWait(1);
    stage_SetAnimation(622, -1, -2);
    stage_SetAnimation(623, 1, 0);
    while (stage_ContinueAnimation(617, 618) == 0) {
        _ACTWait(1);
    }
    scpPlayMot(boyGObj, 306);
    scpPlayMot(scpSearchGobj(2549), 963);
    shadow_DispCancel(74, 0);
    while (stage_CheckAnimationFrame(618, 125, 0) == 0) {
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
        stage_SetAnimation(32, 0, -1);
        stage_SetAnimation(26, 0, -1);
        stage_SetAnimation(35, 0, 0);
        stage_SetAnimation(33, 0, 0);
        stage_SetAnimation(36, 0, 0);
        stage_SetAnimation(38, 0, 0);
        after_mes[0].func = actDejaAfterChk;
        act->mail = after_mes;
        ACTSendMailCorrect(self, 430);
        _ACTWait(0);
    } else {
        stage_SetAnimation(32, 0, -1);
        stage_SetAnimation(26, 0, -1);
        stage_SetAnimation(35, 0, 0);
        stage_SetAnimation(33, 0, 0);
        stage_SetAnimation(36, 0, 0);
        stage_SetAnimation(38, 0, 0);
    }
}

void actDejaAfterChk(GObj *volatile self)
{
    Vec16 target;
    Vec16 dir;

    CheckPoint();
    gflagOn(7);
    lt_switch_layout(55);
    scpBoyControlReadDisable = 1;
    scpPlayStart(boyGObj);
    stage_SetAnimation(624, 1, 0);
    scpPlayMot(boyGObj, 307);
    while (stage_CheckAnimationFinish(624) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);
    scpPlayMot(boyGObj, 0);
    target = afterChkPos;
    sceVu0SubVector(dir.f, target.f, test_CURRENTROOT(boyGObj));
    scpPlayMotDir(boyGObj, dir.f);
    scpPlayEnd(boyGObj);
    scpBoyControlReadDisable = 0;
    lt_switch_layout(54);
    _ACTWait(60);
    if (deja != 0) {
        scpAdpcmFadeCloseFunc(&deja, 80);
    }
    deja = 0;
    _ACTWait(0);
}
