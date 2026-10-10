#include "StageManager.h"
#include "layout_texture.h"
#include "gobj.h"
#include "kanbanBoot.h"
#include <libscf.h>
#include "kanban.h"
#include "GsBase.h"
#include "main.h"
#include "Basic.h"
#include "debug.h"

/* .sdata: the boot sequence's step, the
   card check's step, the start request, the card retry count, the boot sign's
   done flag and kanbanBootEnd. */
static int bootStep = 0; /* derived name */

static int mcCheckStep = 0; /* derived name */

static int bootStarted = 0; /* derived name */

static int mcRetryCount = 10; /* derived name */

static int bootKanbanDone = 0; /* derived name */

int kanbanBootEnd = 0;

inline void kanbanBootInit(void)
{
    bootStep = 0;
    systemStatus[11] = 0;
    kanbanBootEnd = 0;
    fadeStatus = 0;
    bootStarted = 0;
}

/* .bss: the boot-time memory-card request block */
static McMgr bootMcReq; /* derived name */

/* .sbss */
static Kanban *bootKanban; /* derived name */ /* the sign the boot sequence is showing */

static Kanban *bootKanbanSub; /* derived name */ /* the second sign shown beside it */

/* the sign id the card check picked: 3 none, 0 ok, 4 full, -1 clear */
static int mcKanbanId; /* derived name */

static int mcPort; /* derived name */ /* the card slot being checked, 0 then 1 */

static int bootVideoMode; /* derived name */ /* the video mode in force when the sign went up */

/* mcard.c's request entry points; they return iosMsgSend's result, which
   this TU reads only from iosMcSync: it declares the others void, which its
   calls pin, so it does not include mcard.h */
extern void iosMcChdirProduct(McMgr *mp);
extern int iosMcSync(McMgr *mp);
extern void iosMcLoadProductBlock(McMgr *mp);

/* PC port: the language and 50/60 Hz screens are skipped, and so is all
   that followed them: main.c sets both values before stage 1 loads and the
   GS starts (port/config/sysconf.h ico_boot_language, ico_boot_video_mode),
   and the Settings menu changes them later. Steps 96 and 101 below go to
   the end of the check, so steps 102 and 190 to 202 never run: no reload
   of stage 1 for the language's textures, no gsResetFunc, no second card
   check. On the PS2 the signs' black backdrop covered those frames; the
   host draws no sign there, and they showed as flashes.

   So the card check keeps the frame buffer it took at step 2 (fbKeep 1:
   the frame replays lists 11 and 12 only, over the black DISPLAY of the
   load) until it lets go with the first sign of its own, in the same tick
   as on the PS2: the Sony presents sign (the default case, which returns
   to bootStep 2's kanbanReqAdd(2, 1); kanbanExec draws its backdrop in
   that tick's frame, icoMisc.c) or the card warning (step 301).  On the
   PS2 the stage manager let go of it as stage 1's load ended
   (StageManager.c, the fadeOut 0 path), and the next frames drew stage 1
   with only the loading layout's black (alpha 127 of 128) and then the
   language sign's backdrop over it; here those frames (the step 100 to
   101 tick, 101 and 300) had the loading layout's black alone.  The stage
   manager asks ico_kanban_boot_holds_keep before it lets go.  No step
   waits on fbKeep, so every step keeps its tick. */

int ico_kanban_boot_holds_keep(void)
{
    return bootStep == 2;
}

static int kanbanBootMcCheck(void)
{
    McMgr *mc = &bootMcReq;
    McProductFile *r;
    int *lp;
    int lang;
    int ret = 0;

    switch (mcCheckStep) {
    case 0:
        bootKanbanSub = 0;
        /* fallthrough */
    case 1:
        mcPort = 0;
        mcKanbanId = 3;
        mcCheckStep++;
        /* fallthrough */
    case 2:
        fbKeep = 1;
        mc->port = mcPort;
        mc->slot = 0;
        mc->flags.ll &= ~2;
        iosMcChdirProduct(mc);
        mcCheckStep++;
        /* fallthrough */
    case 3:
        if (iosMcSync(mc) != 0) {
            mcCheckStep++;
        }
        break;
    case 4:
        if (mc->type == 2) {
            mcKanbanId = 0;
            if (mc->result == 0 && bootKanbanDone == 0) {
                mcCheckStep = 95;
                break;
            }
            if (mc->format == 0 || mc->result == 0 || mc->free >= 360) {
                mcCheckStep = 100;
                break;
            }
            mcKanbanId = 4;
        }
        if (mcPort == 0) {
            mcPort = 1;
            mcCheckStep = 2;
        } else {
            mcCheckStep = 90;
        }
        break;
    case 90:
        if (mcKanbanId == 3) {
            if (--mcRetryCount > 0) {
                mcCheckStep = 1;
                break;
            }
        }
        mcRetryCount = 0;
        mcCheckStep = 100;
        break;
    case 95:
        iosMcLoadProductBlock(mc);
        mcCheckStep++;
        break;
    case 96:
        if (iosMcSync(mc) == 0) {
            break;
        }
        if (mc->result != 0) {
            mcCheckStep = 100;
            break;
        }
        /* PC port: the card's language and video mode are not applied: the
           ones main.c set stay (the config's, which the Settings menu
           writes), with no gsResetFunc and no reload (step 97 -> 190); on
           to the end through step 100's wait */
        mcCheckStep = 100;
        break;
        mcCheckStep++;
        r = &IosMcProductFile[mc->port];
        NonLinearCameraMove = r->cameraMove;
        systemStatus[0] = r->palMode;
        gsResetFunc(0);
        break;
    case 97:
        if (systemStatus[6] != 0) {
            break;
        }
        mcCheckStep = 190;
        break;
    case 100:
        if (systemStatus[6] != 0) {
            break;
        }
        mcCheckStep = 101;
        break;
    case 101:
        /* PC port: no language screen (main.c set the language), so no
           reload of stage 1 and no second card check (step 200 -> 202 ->
           1): straight to the end, with the card's state this check found */
        bootKanbanDone = 1;
        mcCheckStep = 300;
        break;
        if (bootKanbanDone != 0) {
            mcCheckStep = 300;
            break;
        }
        mcKanbanId = -1;
        lang = sceScfGetLanguage();
        lp = &texLayout[0].defaultItem;
        switch (lang) {
        case 1:
            *lp = 26;
            break;
        case 2:
            *lp = 27;
            break;
        case 3:
            *lp = 30;
            break;
        case 4:
            *lp = 28;
            break;
        case 5:
            *lp = 29;
            break;
        }
        bootKanban = kanbanReqAdd(0, 2);
        mcCheckStep++;
        break;
    case 102:
        if (bootKanban->key != 1) {
            break;
        }
        switch (bootKanban->layout->curItem) {
        case 26:
            NonLinearCameraMove = 2;
            break;
        case 27:
            NonLinearCameraMove = 3;
            break;
        case 28:
            NonLinearCameraMove = 4;
            break;
        case 29:
            NonLinearCameraMove = 5;
            break;
        case 30:
            NonLinearCameraMove = 6;
            break;
        }
        kanbanReqDelFade(bootKanban);
        mcCheckStep = 190;
        bootKanbanDone = 1;
        break;
    case 190:
        mcCheckStep = 191;
        break;
    case 191:
        stgmgrForceSwitchWithFade(1, 255.0f, 0.0f);
        lt_switch_layout(7);
        /* fallthrough */
    case 192:
    case 193:
        mcCheckStep++;
        break;
    case 194:
        if (systemStatus[6] != 0) {
            break;
        }
        if (mcKanbanId != 0) {
            mcCheckStep = 200;
        } else {
            mcCheckStep = 300;
        }
        isysGObjActiveLink(0, 1);
        break;
    case 200:
        bootKanban = kanbanReqAdd(1, 2);
        bootVideoMode = systemStatus[0];
        mcCheckStep++;
        break;
    case 201:
        switch (bootKanban->layout->curItem) {
        case 33:
            systemStatus[0] = 1;
            break;
        case 34:
            systemStatus[0] = 0;
            break;
        }
        if (bootVideoMode != systemStatus[0]) {
            bootVideoMode = systemStatus[0];
            gsResetFunc(0);
        }
        if (bootKanban->key != 1) {
            break;
        }
        kanbanReqDelFade(bootKanban);
        mcCheckStep++;
        break;
    case 202:
        if (mcKanbanId != 0) {
            mcCheckStep = 1;
        } else {
            mcCheckStep = 300;
        }
        isysGObjActiveLink(0, 1);
        break;
    case 300:
        if (mcKanbanId == 0) {
            mcCheckStep = -1;
            break;
        }
        mcCheckStep = 301;
        /* fallthrough */
    case 301:
        fbKeep = 0;
        fadeStatus = 0;
        bootKanban = kanbanReqAdd(5, 2);
        if (bootKanbanSub != 0) {
            kanbanReqDel(bootKanbanSub);
        }
        bootKanbanSub = kanbanReqAdd(mcKanbanId, 1);
        mcCheckStep++;
        break;
    case 302:
        if (bootKanban->key != 1) {
            break;
        }
        if (bootKanban->layout->curItem == 41) {
            mcCheckStep = -1;
            kanbanReqDelFade(bootKanbanSub);
            kanbanReqDelFade(bootKanban);
            break;
        }
        kanbanReqDelFade(bootKanbanSub);
        kanbanReqDelFade(bootKanban);
        mcCheckStep = 1;
        break;
    default:
        fbKeep = 0;
        fadeStatus = 0;
        ret = 1;
        break;
    }
    return ret;
}

static Kanban *waitKanban; /* derived name */ /* the "please wait" sign */

static int waitTimer; /* derived name */ /* frames left on that sign */

/* port/platform/diag_host.c: logs the two steps when they change */
void ico_host_kanban_step(int boot_step, int mc_check_step);

void kanbanBootMain(void)
{
    ico_host_kanban_step(bootStep, mcCheckStep);
    switch (bootStep) {
    case 0:
        isysGObjActiveLink(0, 1);
        systemStatus[5] = 0;
        mcCheckStep = 0;
        bootStep++;
        /* fallthrough */
    case 1:
        if (bootStarted != 0) {
            bootStep++;
        }
        break;
    case 2:
        if (kanbanBootMcCheck() == 0) {
            return;
        }
        kanbanReqAllDelFade();
        waitKanban = kanbanReqAdd(2, 1);
        bootStep++;
        break;
    case 3:
        waitTimer = (60 - systemStatus[0] * 10) / systemStatus[1] * 5;
        bootStep++;
        /* fallthrough */
    case 4:
        waitTimer--;
        if (waitTimer != -1) {
            break;
        }
        bootStep++;
        /* fallthrough */
    case 5:
        if (systemStatus[6] != 0) {
            return;
        }
        bootStep++;
        break;
    case 6:
        kanbanReqAllDelFade();
        bootStep++;
        break;
    case 7:
        if (waitKanban->layout != 0) {
            return;
        }
        kanbanBootEnd = 1;
        bootStep++;
        break;
    }
}

inline void kanbanBootStart(void)
{
    bootStarted = 1;
}
