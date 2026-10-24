#include "op.h"
#include "StageManager.h"
#include "debug.h"
#include "layout_texture.h"
#include "commonact.h"
#include "jimaku.h"
#include "camera-root.h"
#include "gflag.h"
#include "StageAnimation.h"
#include "act.h"
#include "e3.h"
#include "typedef.h"
#include "kanbanBoot.h"
#include "layout_action.h"
#include "thread.h"
#include "gobj_process.h"
#include "main.h"
#include "script.h"
#include "ico_gamestate.h" /* port: achievement signals */

/* .sbss: titleSubEnd and demoSubEnd are the flags a title or demo sub-thread
   raises when it is done and its parent waits on, titleSubAdpcm and demoAdpcm
   the stream handles scpAdpcmPlayRequestFunc fills. */
static int titleSubEnd; /* derived name */

static struct SqEntry *titleSubAdpcm; /* derived name */

static int demoSubEnd; /* derived name */

static struct SqEntry *demoAdpcm; /* derived name */

/* .sdata: the opening demo's step and the step it returns to, ahead of the
   demo's "mode" traces; the globals follow the demo below. */
static int opDemoMode = 0; /* derived name */

static int opDemoNextMode = 0; /* derived name */

void actTitleCamera2(GObj *volatile self)
{
    GObj *x = self;

    enable_game_pause = 1;

    opTitleLogoMode = 0;
    actInitialize(self);
    _ACTWait(1);

    while (1) {
        switch (opTitleLogoMode) {
        case 1:
            stage_SetAnimation(0, 1, 0);
            while (stage_CheckAnimationFinish(0) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            while (opTitleLogoMode == 1) {
                _ACTWait(1);
            }
            break;

        case 2:
            stage_SetAnimation(2, 1, 0);
            while (stage_CheckAnimationFinish(2) == 0) {
                _ACTWait(1);
            }
            _ACTWait(1);
            while (opTitleLogoMode == 2) {
                _ACTWait(1);
            }
            break;

        case 0:
            stage_SetAnimation(2, 0, -1);
            while (opTitleLogoMode == 0) {
                _ACTWait(1);
            }
            break;

        default:
            _ACTWait(1);
            break;
        }
    }
}

void actTitleReadTimeDemo0(GObj *volatile self);
void actTitleShortCut(GObj *volatile self);

/* The timer countdown (tick) was a GNU nested function: it reads and writes
   the parent's `t`, now passed by pointer, and is inlined at both of
   its calls.  The tail after each demo (the thread priority and the fade out)
   is written out in case 0 and again in case 1. */
static inline int tick(int *pt) /* derived name */
{
    if ((current_layout_id == PAL_JPNKOR(12, 9) || current_layout_id == PAL_JPNKOR(13, 10)) &&
        lt_continue_selected == 0) {
        (*pt)--;
    } else {
        *pt = (60 - systemStatus[0] * 10) / systemStatus[1] * 10;
    }
    if (*pt < 0) {
        current_layout_id = PAL_JPNKOR(55, 52);
        return 1;
    }
    return 0;
}

/* PC port: port/game/options.c: [gameplay] skip_opening */
extern int ico_opt_skip_opening(void);

/* PC port: actOpDemo01 has run once (Skip the opening applies to the boot's
   first entry only) */
static int opDemoEntered; /* port */

/* PC port: port/platform/diag_host.c: a line in the log */
extern void ico_host_milestone(const char *what);

void actOpDemo01(GObj *volatile self)
{
    GObj *x = self;
    GProc *th;
    int t = (60 - systemStatus[0] * 10) / systemStatus[1] * 10;

    actInitialize(self);
    _ACTWait(1);

    gFlagGameClear = 0;
    optionScreenMode = 0;
    girlControlMode = 0;
    stgmgrNextStagePreLoadForceStageSet(0);

    actCreateSubThread(actSubMpegReturnPreload, 21);

    actCreateSubThread(actSt26aConte01_1_newgame, 21);

    scpBoyControlReadDisable = 1;

    while (kanbanBootEnd == 0) {
        _ACTWait(1);
    }

    scpFadeOut(255.0f, 0, 0, 0);
    titleAdpcm = 0;

    /* PC port: Skip the opening: the boot's first entry starts at the
       title, in the state the Start skip of the opening scene leaves
       (mode 0 below); later entries (End Game, the attract movie's
       return) are untouched.  It first waits, as the scene does before
       it reads Start, until the boot's layout has handed over to the
       scene layout (la_boot_memory_card_check's switch, in the tick after
       kanbanBootEnd) and that layout's 1 s fade-in is done: mode 1's
       lt_switch_layout to the title is dropped while a fade runs, and
       without the wait it would come two ticks after the end (the wait
       for scpFadeOut(255) below), during that fade-in, and the title
       would come up without its menu */
    if (opDemoEntered == 0) {
        opDemoEntered = 1;
        if (ico_opt_skip_opening() && opDemoMode == 0) {
            opDemoMode = 1;
            opDemoNextMode = 2;
            while (lt_host_boot_settled() == 0) {
                _ACTWait(1);
            }
            ico_host_milestone("boot: Skip the opening: the menu screen has settled, the title's "
                               "menu is asked for");
        }
    }

    while (1) {
        titleSubEnd = 0;
        while (scpFadeChk() != 0) {
            _ACTWait(1);
        }
        opTitleLogoMode = 0;

        if (ICO_JPNKOR) {
            stage_SetAnimation(567, -1, -2);
            stage_SetAnimation(559, -1, -2);
            stage_SetAnimation(560, -1, -2);
            stage_SetAnimation(561, -1, -2);
        } else {
            stage_SetAnimation(571, -1, -2);
        }
        stage_SetAnimation(563, -1, -2);
        stage_SetAnimation(564, -1, -2);
        stage_SetAnimation(565, -1, -2);
        if (!ICO_JPNKOR) {
            stage_SetAnimation(567, -1, -2);
            stage_SetAnimation(568, -1, -2);
            stage_SetAnimation(569, -1, -2);
        }

        switch (opDemoMode) {
        case 0:
            debug_StdPrintfDummy("mode 0");
            current_layout_id = PAL_JPNKOR(55, 52);

            if (titleAdpcm != 0) {
                scpAdpcmFadeCloseFunc(&titleAdpcm, 288);
            }
            titleAdpcm = 0;
            titleSubAdpcm = 0;
            th = actCreateSubThread(actTitleReadTimeDemo0, 21);

            t = (60 - systemStatus[0] * 10) / systemStatus[1] * 10;
            while (titleSubAdpcm == 0) {
                _ACTWait(1);
            }
            _ACTWait(30);

            while (1) {
                _ACTWait(1);
                if (titleSubEnd == 0) {
                    if (pad[0].flags & 0x800) {
                        opDemoMode = 1;
                        opDemoNextMode = 2;
                        break;
                    }
                } else if (tick(&t)) {
                    opDemoMode = 2;
                    break;
                }
            }

            if (titleSubAdpcm != 0) {
                scpAdpcmCloseFunc(&titleSubAdpcm);
            }
            iosThreadSetPri(&((GProc *)th)->thread, 34);
            scpFadeOut(16.0f, 0, 0, 0);
            break;

        case 1:
            debug_StdPrintfDummy("mode 1");
            opTitleLogoMode = 1;
            lt_switch_layout(PAL_JPNKOR(12, 9));
            th = actCreateSubThread(actTitleShortCut, 21);

            t = (60 - systemStatus[0] * 10) / systemStatus[1] * 10;

            while (1) {
                _ACTWait(1);
                if (titleSubEnd != 0 && tick(&t)) {
                    lt_switch_layout(PAL_JPNKOR(55, 52));
                    opDemoMode = opDemoNextMode;
                    break;
                }
            }
            iosThreadSetPri(&((GProc *)th)->thread, 34);
            scpFadeOut(16.0f, 0, 0, 0);
            break;

        case 2:
            debug_StdPrintfDummy("mode 2");
            opDemoMode = 1;
            opDemoNextMode = 0;
            mpegPlayReturnStage = 1;
            if (titleAdpcm != 0) {
                scpAdpcmFadeCloseFunc(&titleAdpcm, 1024);
                while (scpAdpcmCloseChkFunc(&titleAdpcm) != 0) {
                    _ACTWait(1);
                }
            }
            titleAdpcm = 0;
            _ACTWait(1);
            if (ICO_JPNKOR) {
                stgmgrForceSwitchWithFade(57, 256.0f, 4.0f);
            } else {
                stgmgrForceSwitchWithFade(systemStatus[0] != 0 ? 58 : 57, 256.0f, 4.0f);
            }
            _ACTWait(0);
            break;
        }
    }
}

/* .sdata, after actOpDemo01's traces: the second demo's and the first
   scene's stream handles, the title logo's step and the title's stream handle
   (declared in op.h). */
struct SqEntry *op2 = 0;

struct SqEntry *adpcm_conte01_sea = 0;

int opTitleLogoMode = 0;

struct SqEntry *titleAdpcm = 0;

void actTitleShortCut(GObj *volatile self)
{
    GObj *x = self;

    monitorCameraHold = 1;
    actInitialize(self);
    _ACTWait(1);

    if (boyGObj != 0) {
        scpPlayStart(boyGObj);
    }
    if (titleAdpcm == 0) {
        scpAdpcmPlayRequestFunc(56, &titleAdpcm, 0, 0, 1);
    }
    stage_SetAnimation(PAL_JPNKOR(571, 567), 1, 1351);

    SetHandCameraLimitInDemo(0, 0);
    SetZoomMaxValInDemo(0);

    scpSearchGobj(43)->active = 1;
    scpSearchGobj(44)->active = 1;
    scpSearchGobj(45)->active = 1;
    scpSearchGobj(48)->active = 1;
    scpSearchGobj(49)->active = 1;
    scpSearchGobj(50)->active = 1;
    scpSearchGobj(51)->active = 1;

    while (titleAdpcm != 0) {
        _ACTWait(1);
    }

    scpFadeIn(3.0f);

    while (stage_CheckAnimationFinish(PAL_JPNKOR(571, 567)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    titleSubEnd = 1;
    _ACTWait(0);
}

/* .data: the two demo mail pairs, each the mail the demo thread answers and
   the 429 end marker. */
static ActMail opDemo02_mes[2] = {{430}, {429}}; /* derived name */

static ActMail opDemo03_mes[2] = {{430}, {429}}; /* derived name */

inline void actSubMpegReturnPreload(GObj *volatile self)
{
    _ACTWait((int)((float)((60 - systemStatus[0] * 10) / systemStatus[1]) * 5.0f));
    stgmgrNextStagePreLoadForceStageSet(1);
    stgmgrNextStagePreLoadForceNoCancel(1);
}

void actTitleReadTimeDemo0(GObj *volatile self)
{
    debug_StdPrintfDummy("realtime demo %d\n", frame_count);

    stage_SetAnimation(2, 0, -1);

    monitorCameraHold = 0;

    _ACTWait(1);
    lt_switch_layout(PAL_JPNKOR(55, 52));

    _ACTWait(1);

    scpSearchGobj(43)->active = 0;
    scpSearchGobj(44)->active = 0;
    scpSearchGobj(45)->active = 0;
    scpSearchGobj(48)->active = 0;
    scpSearchGobj(49)->active = 0;
    scpSearchGobj(50)->active = 0;
    scpSearchGobj(51)->active = 0;

    scpSearchGobj(46)->active = 1;

    scpAdpcmPlayRequestFunc(6, &titleSubAdpcm, 1, 1, 1);
    while (titleSubAdpcm == 0) {
        _ACTWait(1);
    }

    scpFadeIn(3.0f);

    ResetHandCameraLimitInDemo();
    ResetZoomMaxValInDemo();

    stage_SetAnimation(PAL_JPNKOR(563, 559), 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKOR(563, 559), PAL_JPNKOR(564, 560)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(59), 1051);
    scpSearchGobj(59)->active = 1;
    scpPlayMot(scpSearchGobj(60), 1058);
    scpSearchGobj(60)->active = 1;
    scpPlayMot(scpSearchGobj(61), 1065);
    scpSearchGobj(61)->active = 1;

    while (stage_ContinueAnimation(PAL_JPNKOR(564, 560), PAL_JPNKOR(565, 561)) == 0) {
        _ACTWait(1);
    }

    scpPlayStart(boyGObj);

    scpPlayMot(boyGObj, 263);

    scpPlayMot(scpSearchGobj(56), 983);
    scpSearchGobj(56)->active = 1;
    scpPlayMot(scpSearchGobj(57), 1005);
    scpSearchGobj(57)->active = 1;
    scpPlayMot(scpSearchGobj(58), 1028);
    scpSearchGobj(58)->active = 1;
    scpPlayMot(scpSearchGobj(59), 1052);
    scpPlayMot(scpSearchGobj(60), 1059);
    scpPlayMot(scpSearchGobj(61), 1066);

    while (stage_ContinueAnimation(PAL_JPNKOR(565, 561), PAL_JPNKOR(566, 562)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 264);
    scpPlayMot(scpSearchGobj(56), 984);
    scpPlayMot(scpSearchGobj(57), 1006);
    scpPlayMot(scpSearchGobj(58), 1029);
    scpPlayMot(scpSearchGobj(59), 1053);
    scpPlayMot(scpSearchGobj(60), 1060);
    scpPlayMot(scpSearchGobj(61), 1067);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(566, 562), 60, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpSearchGobj(60)->active = 0;
    scpSearchGobj(57)->active = 0;

    while (stage_ContinueAnimation(PAL_JPNKOR(566, 562), PAL_JPNKOR(567, 563)) == 0) {
        _ACTWait(1);
    }

    scpSearchGobj(60)->active = 1;
    scpSearchGobj(57)->active = 1;

    scpPlayMot(boyGObj, 265);
    scpPlayMot(scpSearchGobj(56), 985);
    scpPlayMot(scpSearchGobj(57), 1007);
    scpPlayMot(scpSearchGobj(58), 1030);
    scpPlayMot(scpSearchGobj(59), 1054);
    scpPlayMot(scpSearchGobj(60), 1061);
    scpPlayMot(scpSearchGobj(61), 1068);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(567, 563), 90, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpSearchGobj(54)->active = 0;

    while (stage_ContinueAnimation(PAL_JPNKOR(567, 563), PAL_JPNKOR(568, 564)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(56), 986);
    scpPlayMot(scpSearchGobj(57), 1008);
    scpPlayMot(scpSearchGobj(58), 1031);
    scpPlayMot(scpSearchGobj(59), 1055);
    scpPlayMot(scpSearchGobj(60), 1062);
    scpPlayMot(scpSearchGobj(61), 1069);

    _ACTWait(1);

    scpSearchGobj(57)->active = 0;

    while (stage_ContinueAnimation(PAL_JPNKOR(568, 564), PAL_JPNKOR(569, 565)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 267);
    scpPlayMot(scpSearchGobj(56), 987);
    scpPlayMot(scpSearchGobj(57), 1009);
    scpPlayMot(scpSearchGobj(58), 1032);
    scpPlayMot(scpSearchGobj(59), 1056);
    scpPlayMot(scpSearchGobj(60), 1063);
    scpPlayMot(scpSearchGobj(61), 1070);

    _ACTWait(1);

    scpSearchGobj(54)->active = 1;
    scpSearchGobj(57)->active = 1;

    scpSearchGobj(43)->active = 1;
    scpSearchGobj(44)->active = 1;
    scpSearchGobj(45)->active = 1;
    scpSearchGobj(48)->active = 1;
    scpSearchGobj(49)->active = 1;
    scpSearchGobj(50)->active = 1;
    scpSearchGobj(51)->active = 1;

    scpSearchGobj(46)->active = 0;

    while (stage_ContinueAnimation(PAL_JPNKOR(569, 565), PAL_JPNKOR(571, 567)) == 0) {
        _ACTWait(1);
    }

    SetHandCameraLimitInDemo(0, 0);
    SetZoomMaxValInDemo(0);

    scpPlayMot(boyGObj, 268);
    scpPlayMot(scpSearchGobj(56), 988);
    scpPlayMot(scpSearchGobj(57), 1010);
    scpPlayMot(scpSearchGobj(58), 1033);
    scpPlayMot(scpSearchGobj(59), 1057);
    scpPlayMot(scpSearchGobj(60), 1064);
    scpPlayMot(scpSearchGobj(61), 1071);

    debug_StdPrintfDummy("finish anim %d\n", frame_count);

    debug_StdPrintfDummy("frame anim %d\n", frame_count);

    _ACTWait(900);

    _ACTWait(1000);

    opTitleLogoMode = 1;
    while (stage_CheckAnimationFrame(PAL_JPNKOR(571, 567), 1300, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    monitorCameraHold = 1;

    debug_StdPrintfDummy("game mode %d %d\n", frame_count, gflagChk(386));

    lt_switch_layout(PAL_JPNKOR(12, 9));

    if (titleAdpcm == 0) {
        scpAdpcmPlayRequestFunc(56, &titleAdpcm, 0, 0, 1);
    }
    titleSubEnd = 1;

    _ACTWait(0);
}

inline void actSt26aConte01_1_newgame(GObj *volatile self)
{
    _ACTWait(1);

    while (gflagChk(382) == 0) {
        _ACTWait(1);
    }

    debug_StdPrintfDummy("newgame demo\n");

    debug_StdPrintfDummy("demo layout\n");

    gflagOn(2);

    RequestStageChange(1, boyGObj, 0, 0.25f, 2.0f);
}

void actOpDemo01_2(GObj *volatile self)
{
    GObj *x = self;
    actInitialize(self);
    _ACTWait(1);

    scpFadeOut(255.0f, 0, 0, 0);

    adpcm_conte01_sea = 0;
    op2 = 0;

    lt_switch_layout(PAL_JPNKOR(55, 52));
    scpBoyControlReadDisable = 1;

    scpAdpcmPlayRequestFunc(7, &adpcm_conte01_sea, 0, 0, 1);
    while (adpcm_conte01_sea != 0) {
        _ACTWait(1);
    }
    scpFadeIn(2.0f);

    actCreateSubThread(actOpDemo01_2Chk, 21);

    demoSubEnd = 0;
    while (demoSubEnd == 0) {
        if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
            break;
        }
        _ACTWait(1);
    }
    ico_gs_signal(ICO_GS_EV_DEMO_END, 2 + (demoSubEnd == 0));

    if (adpcm_conte01_sea != 0) {
        scpAdpcmFadeCloseFunc(&adpcm_conte01_sea, 128);
    }

    RequestStageChangeWithColor(1, boyGObj, 0, 1.0f, 4.0f, 255, 255, 255);
}

void actOpDemo01_2Chk(GObj *volatile self)
{
    stgmgrNextStagePreLoadForceStageSet(0);

    scpPlayStart(boyGObj);

    stage_SetAnimation(PAL_JPNKOR(572, 568), 1, 0);

    stgmgrNextStagePreLoadForceStageSet(exitData[stageData[stage_no].ent[0]].nextStage);

    stage_SetAnimation(7, 1, 0);

    scpPlayMot(boyGObj, 269);
    scpPlayMot(scpSearchGobj(2312), 989);
    scpSearchGobj(2312)->active = 1;
    scpPlayMot(scpSearchGobj(2313), 1011);
    scpSearchGobj(2313)->active = 1;
    scpPlayMot(scpSearchGobj(2314), 1034);
    scpSearchGobj(2314)->active = 1;

    while (stage_ContinueAnimation(PAL_JPNKOR(572, 568), PAL_JPNKOR(573, 569)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 270);
    scpPlayMot(scpSearchGobj(2312), 990);
    scpPlayMot(scpSearchGobj(2313), 1012);
    scpPlayMot(scpSearchGobj(2314), 1035);

    _ACTWait(1);

    stage_SetAnimation(8, 1, 0);

    while (stage_CheckAnimationFrame(PAL_JPNKOR(573, 569), 340, 0) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    scpFadeOut(3.0f, 255, 255, 255);
    while (scpFadeChk() != 0) {
        _ACTWait(45);
    }
    stage_SetAnimation(PAL_JPNKOR(573, 569), -1, -2);
    scpFadeIn(3.0f);

    stage_SetAnimation(PAL_JPNKOR(574, 570), 1, 0);

    scpAdpcmPlayRequestFunc(10, &op2, 0, 1, 1);

    stage_SetAnimation(9, 1, 0);

    scpPlayMot(boyGObj, 271);
    scpPlayMot(scpSearchGobj(2312), 991);
    scpPlayMot(scpSearchGobj(2313), 1013);
    scpPlayMot(scpSearchGobj(2314), 1036);

    while (stage_ContinueAnimation(PAL_JPNKOR(574, 570), PAL_JPNKOR(575, 571)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 272);
    scpPlayMot(scpSearchGobj(2312), 992);
    scpPlayMot(scpSearchGobj(2313), 1014);
    scpPlayMot(scpSearchGobj(2314), 1037);

    _ACTWait(1);

    stage_SetAnimation(10, 1, 0);

    while (stage_CheckAnimationFinish(PAL_JPNKOR(575, 571)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoSubEnd = 1;
}

void actOpDemo02(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    if (boyGObj != 0) {
        scpPlayMot(boyGObj, 0);
    }

    lt_switch_layout(PAL_JPNKOR(55, 52));
    scpBoyControlReadDisable = 1;

    stage_SetAnimation(PAL_JPNKOR(151, 147), 0, 0);

    scpAdpcmPlayRequestFunc(8, &demoAdpcm, 0, 1, 1);
    while (demoAdpcm == 0) {
        _ACTWait(1);
    }

    if (op2 == 0) {
        scpAdpcmPlayRequestFunc(10, &op2, 0, 1, 1);
    }

    opDemo02_mes[0].func = actOpDemo02Chk;
    act->mail = opDemo02_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

inline void actOpDemo02Chk(GObj *volatile self)
{
    gflagOn(3);

    actCreateSubThread(actSt24aConte01_2, 21);

    actCreateSubThread(actSt24aConte01_2_Jimaku, 21);

    demoSubEnd = 0;
    while (demoSubEnd == 0) {
        if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
            break;
        }
        _ACTWait(1);
    }
    ico_gs_signal(ICO_GS_EV_DEMO_END, 4 + (demoSubEnd == 0));

    if (demoSubEnd == 0) {
        if (demoAdpcm != 0) {
            scpAdpcmFadeCloseFunc(&demoAdpcm, 512);
        }
        if (op2 != 0) {
            scpAdpcmFadeCloseFunc(&op2, 64);
        }
    }

    RequestStageChange(2, boyGObj, 0, 0.5f, 4.0f);
}

void actSt24aConte01_2(GObj *volatile self)
{
    stgmgrNextStagePreLoadForceStageSet(exitData[stageData[stage_no].ent[1]].nextStage);

    scpPlayStart(boyGObj);

    stage_SetAnimation(PAL_JPNKOR(576, 572), 1, 0);

    stage_SetAnimation(11, 1, 0);

    scpPlayMot(boyGObj, 273);

    scpPlayMot(scpSearchGobj(2336), 993);
    scpSearchGobj(2336)->active = 1;
    scpPlayMot(scpSearchGobj(2337), 1015);
    scpSearchGobj(2337)->active = 1;
    scpPlayMot(scpSearchGobj(2338), 1038);
    scpSearchGobj(2338)->active = 1;

    while (stage_ContinueAnimation(PAL_JPNKOR(576, 572), PAL_JPNKOR(577, 573)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 274);
    scpPlayMot(scpSearchGobj(2336), 994);
    scpPlayMot(scpSearchGobj(2337), 1016);

    scpPlayMot(scpSearchGobj(2338), 1041);

    _ACTWait(1);

    stage_SetAnimation(12, 1, 0);

    _ACTWait(120);

    stage_SetAnimation(PAL_JPNKOR(151, 147), 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKOR(577, 573), PAL_JPNKOR(578, 574)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 275);
    scpPlayMot(scpSearchGobj(2336), 995);
    scpPlayMot(scpSearchGobj(2337), 1017);
    scpPlayMot(scpSearchGobj(2338), 1039);

    _ACTWait(1);

    stage_SetAnimation(13, 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKOR(578, 574), PAL_JPNKOR(579, 575)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 276);
    scpPlayMot(scpSearchGobj(2336), 996);
    scpPlayMot(scpSearchGobj(2337), 1018);
    scpPlayMot(scpSearchGobj(2338), 1040);

    _ACTWait(1);

    stage_SetAnimation(14, 1, 0);

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 10);

    scpFadeOut(6.0f, 0, 0, 0);
    while (scpFadeChk() != 0) {
        _ACTWait(45);
    }
    stage_SetAnimation(PAL_JPNKOR(579, 575), -1, -2);
    scpFadeIn(3.0f);

    stage_SetAnimation(PAL_JPNKOR(580, 576), 1, 0);

    stage_SetAnimation(15, 0, 0);

    scpPlayMot(boyGObj, 277);
    scpPlayMot(scpSearchGobj(2336), 997);
    scpPlayMot(scpSearchGobj(2337), 1019);
    scpPlayMot(scpSearchGobj(2338), 1041);

    while (stage_CheckAnimationFinish(PAL_JPNKOR(580, 576)) == 0) {
        _ACTWait(1);
    }
    _ACTWait(1);

    demoSubEnd = 1;
}

inline void actSt24aConte01_2_Jimaku(GObj *volatile self)
{
    float t;
    float tn;
    int n;

    t = 0.0f;
    do {
        switch ((int)t) {
        case 100:
            jimakuBegin(&jimaku_msg);
            break;
        case 2400:
            jimaku_msg.sub.block = 0;
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
    } while (t < 2700.0f);
}

inline void actOpDemo03(GObj *volatile self)
{
    GObj *x = self;
    Act *act = actInitialize(self);
    _ACTWait(1);

    lt_switch_layout(PAL_JPNKOR(55, 52));
    scpBoyControlReadDisable = 1;

    if (!ICO_JPNKOR) {
        stage_SetAnimation(176, 0, 0);
    }
    stage_SetAnimation(172, 0, 0);
    if (ICO_JPNKOR) {
        stage_SetAnimation(168, 0, 0);
    }

    opDemo03_mes[0].func = actOpDemo03Chk;
    act->mail = opDemo03_mes;
    ACTSendMailCorrect(self, 430);
    _ACTWait(0);
}

void actOpDemo03Chk(GObj *volatile self)
{
    float t = 4.0f;

    gflagOn(4);

    scpFadeOut(255.0f, 0, 0, 0);

    scpAdpcmPlayRequestFunc(9, &op2, 0, 1, 1);
    while (op2 == 0) {
        _ACTWait(1);
    }
    scpFadeIn(3.0f);

    actCreateSubThread(actSt13aConte01_3, 21);

    demoSubEnd = 0;
    while (demoSubEnd == 0) {
        if ((pad[0].flags & 0x800) && scpAdpcmPlayRequestNum() == 0) {
            break;
        }
        _ACTWait(1);
    }
    ico_gs_signal(ICO_GS_EV_DEMO_END, 6 + (demoSubEnd == 0));

    if (demoSubEnd == 0) {
        if (op2 != 0) {
            scpAdpcmFadeCloseFunc(&op2, 128);
            t = 16.0f;
        }
    }

    scpFadeOut(t, 0, 0, 0);
    while (scpFadeChk() != 0) {
        _ACTWait(1);
    }

    RequestStageChange(4, boyGObj, 0, 255.0f, 2.0f);
}

void actSt13aConte01_3(GObj *volatile self)
{
    stgmgrNextStagePreLoadForceStageSet(exitData[stageData[stage_no].ent[3]].nextStage);

    scpPlayStart(boyGObj);

    stage_SetAnimation(PAL_JPNKOR(581, 577), 1, 0);

    scpPlayMot(boyGObj, 278);
    scpPlayMot(scpSearchGobj(2364), 998);
    scpSearchGobj(2364)->active = 1;
    scpPlayMot(scpSearchGobj(2365), 1020);
    scpSearchGobj(2365)->active = 1;
    scpPlayMot(scpSearchGobj(2366), 1042);
    scpSearchGobj(2366)->active = 1;

    _ACTWait(1);
    stage_SetAnimation(16, 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKOR(581, 577), PAL_JPNKOR(582, 578)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(scpSearchGobj(2366), 1043);

    scpPlayMot(scpSearchGobj(2364), 999);
    scpPlayMot(scpSearchGobj(2365), 1021);

    _ACTWait(1);
    stage_SetAnimation(17, 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKOR(582, 578), PAL_JPNKOR(583, 579)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 279);
    scpPlayMot(scpSearchGobj(2366), 1044);

    _ACTWait(1);
    stage_SetAnimation(18, 1, 0);
    stage_SetAnimation(PAL_JPNKOR(176, 172), 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKOR(583, 579), PAL_JPNKOR(584, 580)) == 0) {
        _ACTWait(1);
    }

    scpFadeIn(3.0f);

    scpPlayMot(boyGObj, 280);
    scpPlayMot(scpSearchGobj(2364), 999);
    scpPlayMot(scpSearchGobj(2365), 1021);
    scpPlayMot(scpSearchGobj(2366), 1045);

    _ACTWait(1);
    stage_SetAnimation(19, 1, 0);
    stage_SetAnimation(PAL_JPNKOR(172, 168), 1, 0);

    while (stage_ContinueAnimation(PAL_JPNKOR(584, 580), PAL_JPNKOR(585, 581)) == 0) {
        _ACTWait(1);
    }

    scpPlayMot(boyGObj, 281);
    scpPlayMot(scpSearchGobj(2364), 1000);
    scpPlayMot(scpSearchGobj(2365), 1022);
    scpPlayMot(scpSearchGobj(2366), 1046);

    _ACTWait(1);
    stage_SetAnimation(20, 1, 0);

    _ACTWait((60 - systemStatus[0] * 10) / systemStatus[1] * 8);

    demoSubEnd = 1;
    _ACTWait(0);
}
