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

#ifdef ICO_HOST

/* PC port (Phase 6, 6C): the language and 50/60 Hz screens are skipped; the
   values they stored come from the port's config (port/config/sysconf.h,
   docs/port/SETTINGS.md), and the Settings menu changes them later */
int ico_boot_language(void);
int ico_boot_video_mode(int current);
int ico_boot_card_language(int card);
int ico_boot_card_video_mode(int card);

#endif

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
        mcCheckStep++;
        r = &IosMcProductFile[mc->port];
        NonLinearCameraMove = r->cameraMove;
        systemStatus[0] = r->palMode;
#ifdef ICO_HOST
        /* an explicit config value (the Settings menu's) wins over the card */
        NonLinearCameraMove = ico_boot_card_language(NonLinearCameraMove);
        systemStatus[0] = ico_boot_card_video_mode(systemStatus[0]);
#endif
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
        if (bootKanbanDone != 0) {
            mcCheckStep = 300;
            break;
        }
        mcKanbanId = -1;
#ifdef ICO_HOST
        /* no language screen: what step 102 would have stored, from the
           config or the host's locale (sceScfGetLanguage, as the screen's
           cursor) */
        NonLinearCameraMove = ico_boot_language();
        mcCheckStep = 190;
        bootKanbanDone = 1;
        break;
#endif
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
#ifdef ICO_HOST
        /* no 50/60 Hz screen: [video] video_mode, else the screen's default
           item (50 Hz, the value in force), with step 201's reset on a
           change */
        bootVideoMode = systemStatus[0];
        systemStatus[0] = ico_boot_video_mode(systemStatus[0]);
        if (bootVideoMode != systemStatus[0]) {
            bootVideoMode = systemStatus[0];
            gsResetFunc(0);
        }
        mcCheckStep = 202;
        break;
#endif
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

#ifdef ICO_HOST

/* port/platform/diag_host.c: logs the two steps when they change */
void ico_host_kanban_step(int boot_step, int mc_check_step);

#endif

void kanbanBootMain(void)
{
#ifdef ICO_HOST
    ico_host_kanban_step(bootStep, mcCheckStep);
#endif
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
