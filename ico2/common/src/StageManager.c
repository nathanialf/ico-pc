#include "StageManager.h"
#include "backStage.h"
#include "debug.h"
#include "debug_menu.h"
#include "gamesys.h"
#include "icoMisc.h"
#include "layout_texture.h"
#include "cdvd.h"
#include "memory.h"
#include "message.h"
#include "thread.h"
#include "gobj.h"
#include "isys.h"
#include "s_init.h"
#include "soundManager.h"
#include "fieldCollision.h"
#include "access.h"
#include "fightSound.h"
#include "warpGirl.h"
#include "DisplayP2O.h"
#include "GsBase.h"
#include "Matrix.h"
#include "darkVolume.h"
#include "delayFreeManager.h"
#include "geometryManager.h"
#include "matrixDrive.h"
#include "motionFileManager.h"
#include "streamMotionManager.h"
#include "tableSin.h"
#include "Basic.h"
#include "ios.h"
#include "jimaku.h"
#include "kanbanBoot.h" /* PC port: ico_kanban_boot_holds_keep */

typedef struct { /* field names derived */
    int stage;
    float dist;
    unsigned char pad8[8];
    float pos[4];
} StgSlot; /* derived name */

/* .data, all zero: the preload buffer, 896 sectors of 2048
   bytes, the cap stgmgrNextStagePreLoad clamps a read to and the ring cdvd.c's
   stream reads through; the stage manager's message queue record; and the exit
   positions of the fifteen entrances stgmgrNextStagePreLoadEntry collects. */
char stagePreLoadBuff[896 * 2048] = {0};

IosMsgQueue stageMgrMsgQ = {0};

StgSlot stageExitData[15] = {0};

/* .sbss: the one-entry buffer of the stage manager's message queue, and the
   stage stgmgrNextStagePreLoadForceStageSet asks the preloader for. */
static IosMsgWord stageMgrMsgBuf; /* derived name */

static int stagePreLoadForceStageNo; /* derived name */

/* .bss: the thread descriptor InitIcoMisc is started through, an IOSThread's
   28 words; InitIcoMisc's flag word at 0x3C is read here as an unsigned word,
   where IOSThread's flags is the int ios/thread.c tests */

static IOSThread initIcoMiscThread; /* derived name; wider than 28 words here (pointers) */

#define INITICOMISC_THREAD (&initIcoMiscThread)
#define INITICOMISC_FLAGS ((unsigned int)initIcoMiscThread.flags)

#include "main.h"
#include <libgraph.h>
#include <libvu0.h>
#include <eekernel.h>
#include <libdma.h>
#include <string.h>
#include "ico_gamestate.h" /* port: achievement signals */
#include "ico_credits.h"   /* port: Extras > Credits */

extern void ico_video_camera_cut(void); /* port (renderer R7b): port/game/video_options.c */

#include "typedef.h"

/* .sdata: the movie switches main.c's loop reads, defined before
   stop_free_resources; the preload state before stgmgrNextStagePreLoad; the
   fade speed and the exit count last. */
int mpegPlay = 0;

int mpegInitDone = 0;

int mpegPlayReturnStage = 3;

unsigned int mpegPlayInitColor = 0x80000000;

int stageManagerFreeResourceFlag = 0;

int stgMgrWakeupRequest = 0;

static void stgmgrNextStagePreLoadDiskNotReady(void);

/*SW*/
static void stop_free_resources(void)
{
    int i;

    debug_StdPrintfDummy("----- MASK LINK -----\n");
    game_pause = 0;
    for (i = 0; i < 8; i++) {
        isysGObjActiveLink(i, 0);
    }
    if (stageData[before_stage_no].endproc != 0) {
        stageData[before_stage_no].endproc();
    }
    iosThreadCancelWakeup(0);
    isysGObjRemoveAll();
    sceGsSyncPath(0, 0);
    InitDelayFree();
    if (systemStatus[10] != 0) {
        jimakuEnd(&jimaku_msg);
        systemStatus[10] = 0;
    }
    if (sndInitBgmCancelFlag == 0) {
        debug_StdPrintfDummy("sound partition reset\n");
        iosMallocResetPartition(ios_partition_sound);
    } else {
        debug_StdPrintfDummy("sound partition not reset\n");
    }
    iosMallocResetPartition(ios_partition_seki);
    iosMallocResetPartition(ios_partition_sugipon);
    iosMallocResetPartition(ios_partition_dmotion);
    iosMallocResetPartition(ios_partition_oomori);
    iosMallocResetPartition(ios_partition_isys);
    ResetDynamicMotionManager();
    debug_StdPrintfDummy("here\n");
    InitDelayFree();
    girlGObj = 0;
    boyGObj = 0;
}

/*SW-END*/
/*SW*/
static void stage_initialize(void)
{
    isysInitialize();
    debug_StdPrintfDummy("InitTableSin\n");
    InitTableSin();
    debug_StdPrintfDummy("InitMatrixDrive\n");
    InitMatrixDrive();
    InitGameOverEffect();
    debug_StdPrintfDummy("debug_Init\n");
    gsb_InitGSSystem();
    debug_StdPrintfDummy("p2o transMicroProgram\n");
    p2o_TransMicroProgram();
    debug_StdPrintfDummy("InitGSSystem\n");
    debug_Init();
    debug_StdPrintfDummy("init debug menu\n");
    init_debug_menu();
    debug_StdPrintfDummy("enable vsync\n");
    EnableIntc(2);
}

/*SW-END*/

static void exit_stage(int stage)
{
    gamesysStageExitTimeSet(stage_no);
    warpGirlOutStage(stage_no, 0);
    warpGirlInStage(stage);
    backStageProcessOutStage();
    sndBgmReadyNextStage(stage, stage_no);
    return DeleteStreamMotionManager();
}

/* systemStatus seen as a record by the mpeg arm, which clears its pause flag
   (systemStatus[5], the game paused while it is set) and its stage-change flag
   (systemStatus[6], set by the stage exit, waited on until the stage is up). */
typedef struct MpegRec { /* field names derived */
    int pad0[5];
    int pause;
    int stageChange;
} MpegRec; /* derived name */

static void start_stage_Load_thread(int stage)
{
    before_stage_no = stage_no;
    stage_no = stage;
    ico_gs_signal(ICO_GS_EV_STAGE_ENTER, stage);
    ico_credits_stage_enter(stage); /* port (CRED): the title after the credits */
    ico_video_camera_cut();         /* port (R7b): a stage change is a cut for the presenter */
    gsb_SetBGColor(&db, 1, 1, 1);
    sceGsSyncPath(0, 0);
    stageManagerFreeResourceFlag = 1;
    stop_free_resources();
    if (mpegPlay == 0) {
        long long flags;

        stage_initialize();
        stageManagerFreeResourceFlag = 0;
        iosThreadCancelWakeup(0);
        gsb_SetMotionBlur();
        current_stage_no = stage;
        iosThreadCreateS(INITICOMISC_THREAD, 1, InitIcoMisc, &stage_no, ios_partition_root, 0x18000,
                         27);
        iosThreadStart(INITICOMISC_THREAD);
        flags = INITICOMISC_FLAGS;
        debug_StdPrintfDummy("auto stack %d\n", (int)flags & 1);
        game_pause = 1;
        debug_StdPrintfDummy("-----------------Enable VSync\n");
    } else {
        isysInitialize();
        sceGsResetPath();
        sceVpu0Reset();
        sceDmaReset(1);
        mpegInitDone = 1;
        ((MpegRec *)systemStatus)->pause = 0;
        ((MpegRec *)systemStatus)->stageChange = 0;
        girlGObj = 0;
        boyGObj = 0;
        stageManagerFreeResourceFlag = 0;
    }
}

/* The DEBUG build's preload report and hold (the report's text derived): a
   debug build reports the stage it is about to preload and, while the debug
   flag word's hold bit is set, repeats the report instead of reading.  Both
   build only under DEBUG; in the retail build the report is empty and the
   hold test is 0, so the loop runs once. */
static __inline__ void stgPreLoadDebugHook(void) /* derived name */
{
#ifdef DEBUG
    scePrintf("preload: leaving stage %d's data\n", stagePreLoadStageNo);
#endif
}

static __inline__ int stgPreLoadDebugHold(void) /* derived name */
{
#ifdef DEBUG
    return debug_font_flag & 0x100;
#else
    return 0;
#endif
}

int stagePreLoadStageNo = 0;

int stagePreLoadReadOffset = 0;

int stagePreLoad2ndReadOffset = 0;

static int stagePreLoadWait = 0; /* derived name */

static int stagePreLoadMode = 0; /* derived name */

static int stagePreLoadNoCancel = 0; /* derived name */

static CdvdBgReq *stagePreLoadMgrEntry = 0; /* derived name */

int stagePreLoadLsn = 0;

int stagePreLoadSectorCnt = 0;

static int stgmgrNextStagePreLoad(CdvdBgReq *bg)
{
    float root[4];
    float d[4];
    int size;
    int stage = 0;
    int i;
    float dist;

    if (stagePreLoadWait++ < 15) {
        return 0;
    }
    stagePreLoadWait = 0;
    if (iosCdvdBackGroundMgrEntryNum() >= 3 && stagePreLoadNoCancel == 0) {
        return 0;
    }
    switch (stagePreLoadMode) {
    case 0: {
        int best = -1;

        if (boyGObj == 0) {
            return 0;
        }
        GetRootPosition(root, boyGObj);
        for (i = 0; i < stageExitDataCnt; i++) {
            StgSlot *e;

            _SubVector(d, root, stageExitData[i].pos);
            dist = _InnerProduct(d, d);
            e = &stageExitData[i];
            e->dist = dist;
            if (best == -1) {
                best = i;
                stage = e->stage;
            } else if (dist < (&stageExitData[best])->dist) {
                best = i;
                stage = e->stage;
            }
        }
        break;
    }
    case 1:
        stage = stagePreLoadForceStageNo;
        break;
    }
    if (stage != 0 && stage != stagePreLoadStageNo && stageData[stage].mpegNo == 0) {
        int readSize;
        int ret;

        /* The DEBUG build's report and hold, see stgPreLoadDebugHook. */
        do {
            stgPreLoadDebugHook();
        } while (stgPreLoadDebugHold());
        strcpy(bg->name, GetDataFileName(stage, 1));
        ret = -1;
        iosCdvdChgFileName(bg->name);
        stagePreLoadLsn = bg->lsn = iosCdvdGetFileLsn(bg->name, &size);
        size = (size + 0x7FF) / 0x800 * 0x800;
        readSize = size > 0x1C0000 ? 0x1C0000 : size;
        debug_StdPrintfDummy("preload %s move %d total %d reset %d\n", bg, readSize, size,
                             size - readSize);
        bg->pos = 0;
        ret = iosCdvdBackGroundRead(bg, stagePreLoadBuff, readSize);
        debug_StdPrintfDummy("done");
        stagePreLoadSectorCnt = readSize >> 11;
        stagePreLoadStageNo = stage;
    }
    return 0;
}

static inline void stgmgrNextStagePreLoadDiskNotReady(void)
{
    stagePreLoadStageNo = 0;
    stagePreLoadWait = 0;
    stagePreLoadLsn = 0;
}

float mpegPlayFadeInSpeed = 0.0f;

int stageExitDataCnt = 0;

static void stgmgrNextStagePreLoadEntry(int stage)
{
    const StgPre *pre = &stageData[stage];
    int i;
    CdvdBgReq *ret;

    stageExitDataCnt = 0;
    for (i = 0; i < 15; i++) {
        short s = pre->ent[i];
        if (s != 0) {
            int count = stageExitDataCnt;
            stageExitData[count].stage = exitData[s].nextStage;
            if (PositionOfExit(stageExitData[count].pos, i + 1) == 0) {
                stageExitDataCnt = stageExitDataCnt + 1;
            }
        }
    }
    ret = iosCdvdBackGroundMgrAdd("DFDATAS/COMMON.DF", stgmgrNextStagePreLoad, 0,
                                  stgmgrNextStagePreLoadDiskNotReady, 0, 0, 0, 0);
    stagePreLoadMgrEntry = ret;
    iosCdvdBackGroundMgrNotDiskReadyPauseSet(ret, 1);
    stagePreLoadStageNo = 0;
    stagePreLoadSectorCnt = 0;
    stagePreLoadLsn = 0;
    stagePreLoadReadOffset = 0;
    stagePreLoad2ndReadOffset = 0;
    stagePreLoadWait = 0;
    stagePreLoadMode = 0;
}

inline void stgmgrNextStagePreLoadDistBoyMode(void)
{
    stagePreLoadMode = 0;
    stagePreLoadNoCancel = 0;
}

inline void stgmgrNextStagePreLoadForceStageSet(int val)
{
    stagePreLoadForceStageNo = val;
    stagePreLoadMode = 1;
    stagePreLoadNoCancel = 0;
}

inline void stgmgrNextStagePreLoadForceNoCancel(int val)
{
    stagePreLoadNoCancel = val;
}

void StageManager(void)
{
    StgMgrMsg *msg;

    debug_StdPrintfDummy("stage manager() in\n");
    iosMsgQueueCreate(&stageMgrMsgQ, &stageMgrMsgBuf, 1);
    debug_StdPrintfDummy("IosCdLock %d\n", IosCdLock);
    WaitSema(IosCdLock);
    DeleteSema(IosCdLock);
    SignalSema(IosStgMgrLock);
    debug_StdPrintfDummy("STAGE MANAGER START\n");
    while (1) {
        iosMsgRecv(&stageMgrMsgQ, (IosMsgWord *)&msg, 1);
        mpegPlayFadeInSpeed = 128.0f;
        fadeSpeed = 0;
        switch (msg->cmd) {
        case 0:
            break;
        case 1:
            fadeStatus = 1;
            fadeSpeed = msg->fadeIn;
            fadeColor[0] = msg->r;
            fadeColor[1] = msg->g;
            fadeColor[2] = msg->b;
            fadeColor[3] = 0;
            fadeContinue = 1;
            fbKeep = 1;
            if (stageData[msg->stage].mpegNo != 0) {
                stgMgrWakeupRequest = 1;
                mpegPlayFadeInSpeed = msg->fadeOut;
                do {
                    iosThreadSleep();
                } while (fadeStatus != 3);
            }
            break;
        default:
            goto badCmd;
        }
        if (stageData[msg->stage].mpegNo != 0) {
            mpegInitDone = 0;
            fightSoundClose();
            soundDataSegAllClose(0, 2);
        }
        if (stagePreLoadMgrEntry != 0) {
            iosCdvdBackGroundMgrDelete(stagePreLoadMgrEntry);
        }
        stagePreLoadMgrEntry = 0;
        if (msg->stage <= 0xFFFF) {
            exit_stage(msg->stage);
            lt_switch_layout(0x35);
            systemStatus[6] = 1;
            systemStatus[5] = 1;
            stgMgrWakeupRequest = 1;
            while (iosCdvdBackGroundMgrDeleteRequestGet() != 0) {
                iosThreadSleep();
            }
            stgMgrWakeupRequest = 0;
            mpegPlay = stageData[msg->stage].mpegNo;
            start_stage_Load_thread(msg->stage);
        } else {
            debug_StdPrintfDummy("out of stage %d\n", msg->stage);
        }
        if (msg->fadeOut == 0.0f) {
            fadeStatus = 0;
            stgMgrWakeupRequest = 1;
            while (systemStatus[6] != 0) {
                iosThreadSleep();
            }
            if (mpegPlay == 0) {
                stgmgrNextStagePreLoadEntry(msg->stage);
            }
            /* PC port: stage 1's load at the boot ends while the card check
               runs; the check keeps the frame buffer until its first sign,
               where the PS2's language sign covered the frames between
               (kanbanBoot.c, ico_kanban_boot_holds_keep) */
            if (ico_kanban_boot_holds_keep() == 0) {
                fbKeep = 0;
            }
            stgMgrWakeupRequest = 0;
        } else {
            stgMgrWakeupRequest = 1;
            while (systemStatus[6] != 0) {
                iosThreadSleep();
            }
            if (mpegPlay == 0) {
                stgmgrNextStagePreLoadEntry(msg->stage);
            }
            fadeContinue = 0;
            fadeStatus = 1;
            fbKeep = 0;
            stgMgrWakeupRequest = 0;
            fadeSpeed = -msg->fadeOut;
        }
        continue;
    badCmd:
        debug_StdPrintfDummy("StageManager:unknown msg\n");
    }
    /* unreachable: the loop above never exits */
    debug_StdPrintfDummy("stage manager() out\n");
}

inline void CheckPoint(void)
{
    if (systemStatus[2]) {
        gamesysMemorySave(gameSysMemoryFuncList, gameSysMainSaveBuff, 0);
        systemStatus[3] = 1;
        ico_gs_signal(ICO_GS_EV_CHECKPOINT, stage_no);
    }
}

void stgmgrForceSwitch(int stage)
{
    stageMgrMsg.cmd = 0;
    stageMgrMsg.stage = stage;
    graphics_ready = 1;
    stageMgrMsg.fadeOut = 0;
    iosMsgSend(&stageMgrMsgQ, (IosMsgWord)&stageMgrMsg, 1);
}

void stgmgrForceSwitchWithFade(int stage, float fadeIn, float fadeOut)
{
    stgmgrForceSwitchWithFadeColor(stage, fadeIn, fadeOut, 0, 0, 0);
}

void stgmgrForceSwitchWithFadeColor(int stage, float fadeIn, float fadeOut, unsigned char r,
                                    unsigned char g, unsigned char b)
{
    stageMgrMsg.cmd = 1;
    stageMgrMsg.stage = stage;
    stageMgrMsg.fadeOut = fadeOut;
    stageMgrMsg.fadeIn = fadeIn;
    stageMgrMsg.r = r;
    stageMgrMsg.g = g;
    stageMgrMsg.b = b;
    mpegPlayInitColor = 0x80000000 | (b << 16) | (g << 8) | r;
    graphics_ready = 1;
    iosMsgSend(&stageMgrMsgQ, (IosMsgWord)&stageMgrMsg, 1);
}
