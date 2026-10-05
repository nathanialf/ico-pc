#include "StageManager.h"
#include "mcard.h"
#include "debug.h"
#include "layout_action.h"
#include "cdvd.h"
#include "jimaku.h"
#include "ios.h"
#include "message.h"
#include "gflag.h"
#include "FileManager.h"
#include "GsBase.h"
#include "Matrix.h"
#include "keyInput.h"
#include <eekernel.h>
#include "main.h"
#include <eeregs.h>
#include "s_init.h"
#include "delayFreeManager.h"
#include "adpcm_init.h"
#include "fieldCollision.h"
#include "icoMisc.h"
#include "soundManager.h"
#include "thread.h"
#include "act-game.h"
#include "obj_manager.h"
#include "StageAnimation.h"
#include "mv_main.h"
#include "geometryManager.h"
#include "libgraph.h"
#include <stdlib.h>

#ifdef ICO_HOST

#include "ico_gamestate.h" /* port: achievement signals, docs/port/ACHIEVEMENTS.md */

#endif

/* main.c's .data globals, each with an initialiser. systemStatus starts in PAL mode (word 0)
   at a frame step of 2 (word 1). db is the GS double buffer (libgraph's
   sceGsDBuff, 0x230 B), stageMgrMsg the stage manager's message (main.h's
   StgMgrMsg, 0x18 B) and SchedulerMsgQ the scheduler's queue (message.c's
   IosMsgQueue, 0x30 B). */
int systemStatus[12] = {1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 7};

sceGsDBuff db = {{{0}}};

StageSetting GlobalStageSetting = {{{0}}};

PadState pad[16] = {{0}};

StgMgrMsg stageMgrMsg = {0};

IosMsgQueue SchedulerMsgQ = {0};

/* .sdata: the scheduler's counters and the network load target */
static int vsyncCount = 0; /* derived name */ /* the scheduler's vsync count */

char NetLoadTARGET[] = "host0:";

/* Main's frame-done flag, -1 before the first frame */
static int frameReady = -1; /* derived name */

static int lastVsyncCount = 0; /* derived name */ /* movie_abort_check's last seen vsyncCount */

typedef struct { /* field names derived */
    IOSThread *th[6];
} ThreadTbl; /* derived name */

/* .bss: the record and the stack of every thread this file starts, then the
   scheduler's message buffer, named after jimaku.c's jimakuThread and
   jimakuThreadStack. A thread record is the ios thread object, 0x70 bytes, of
   which this file reads only the kernel id word at +0x30.  Each stack is
   16-byte aligned, as the kernel's CreateThread requires. */
static IOSThread idleThread; /* derived name */

static char idleThreadStack[8192] __attribute__((aligned(16))); /* derived name */

static IOSThread mainThread; /* derived name */

static char mainThreadStack[24576] __attribute__((aligned(16))); /* derived name */

static IOSThread schedulerThread; /* derived name */

static char schedulerThreadStack[4096] __attribute__((aligned(16))); /* derived name */

static IOSThread mcThread; /* derived name */

static char mcThreadStack[8192] __attribute__((aligned(16))); /* derived name */

static IOSThread cdvdThread; /* derived name */

static char cdvdThreadStack[110592] __attribute__((aligned(16))); /* derived name */

static IOSThread stageManagerThread; /* derived name */

static char stageManagerThreadStack[8192] __attribute__((aligned(16))); /* derived name */

static IOSThread soundThread; /* derived name */

static char soundThreadStack[8192] __attribute__((aligned(16))); /* derived name */

static IosMsgWord schedulerMsgBuff[8]; /* derived name */

/* the six threads Emergency_DestroyAllThread tears down, every thread boot
   starts except idle; first in this object's .rodata, ahead of Main's
   strings, so it is defined here with the records it points at */
static const ThreadTbl allThreads = {{&mainThread, &schedulerThread, &mcThread, &cdvdThread,
                                      &stageManagerThread, &soundThread}}; /* derived name */

static void idle(void);
static void scheduler(void);
/* motionOrientManager.h carries MotOriName and declares no movieFile */
extern char movieFile[];
int movie_abort_check(void);

#ifdef ICO_HOST

/* port/platform: the vsync busy-wait (sched.h) and the INTC raise
   (kernel_host.h) */
void ico_sched_spin_vsync(void);
int ico_kernel_raise_intc(int cause);
void ico_vsync(int field_parity);
/* port/platform/trace_host.c: one Main tick done (trace, --ticks, pad script) */
void ico_host_main_tick(void);
/* port/platform/diag_host.c: boot milestones in the log, and the names of
   this file's static thread functions */
void ico_host_milestone(const char *what);
void ico_host_name_func(void *func, const char *name);
/* port/game/options.h: developer mode (renderer wave 6, R6a) */
int ico_opt_developer_mode(void);
/* common/src/debug.c; debug.h declares only debug_Menu_off */
void debug_Menu(void);

#endif

/* the development build's Main also called debug_Menu, debug_SetBar and
   debug_SetBar2 and carried a frame-step block; the retail build compiled
   them out. */
void Main(void)
{
    int ret;
    int n;

    debug_StdPrintfDummy("VSYNC_TIMING    : \x1b[33m%s\x1b[m\n",
                         systemStatus[0] == 0 ? "NTSC" : "PAL");
    debug_StdPrintfDummy("FRAME_STEP      : \x1b[33m%d\x1b[m\n", systemStatus[1]);
    debug_StdPrintfDummy("SYSTEM_FRAMERATE: \x1b[33m%d\x1b[m\n",
                         (60 - systemStatus[0] * 10) / systemStatus[1]);
    *T0_COUNT = 0;
    NonLinearCameraMove = 3;
    stage_no = 0;
    exit_no = 0;
    interlace = 1;
    GlobalTimer = 0;
    systemFault = 0;
    lock_execIcoMisc = 0;
    n = debug_TryToGetStartStage();
    thisIsYourStartStage = n < 106 ? n : 105;
    if (thisIsYourStartStage <= 0) {
        thisIsYourStartStage = 1;
    }
    debug_VariableInit();
    InitDelayFree();
    debug_StdPrintfDummy("Main() in\n");
    debug_StdPrintfDummy("Main() in\n");
    debug_StdPrintfDummy("IosPadLock %d\n", IosPadLock);
    WaitSema(IosPadLock);
    DeleteSema(IosPadLock);
    debug_StdPrintfDummy("IosstgMgrLock %d\n", IosStgMgrLock);
    WaitSema(IosStgMgrLock);
    DeleteSema(IosStgMgrLock);
#ifdef ICO_HOST
    ico_host_milestone("Main: past IosPadLock and IosStgMgrLock");
#endif
    stgmgrForceSwitchWithFade(thisIsYourStartStage < 0 ? 1 : thisIsYourStartStage, 255.0f, 0.0f);
    iosThreadCancelWakeup(0);
    systemStatus[5] = 0;
    _InitRandom(1.2345678f);
    gsb_InitGSSystem();
    debug_StdPrintfDummy("main start\n");
#ifdef ICO_HOST
    ico_host_milestone("Main: gsb_InitGSSystem done, entering the loop");
#endif
    while (1) {
        iosThreadCancelWakeup(0);
        iosThreadSleep();
        gsb_ResetSnap();
        if (mpegPlay != 0) {
            if (mpegInitDone == 0) {
                continue;
            }
            if (iosCdvdBackGroundMgrRunning != 0) {
                continue;
            }
            ios_partition_mpeg = ios_partition_isys;
            AdpcmStreamFree();
            soundAllocIopFree();
            movie_init(&movieFile[mpegPlay * 0x20], 720, systemStatus[0] ? 576 : 480, 36, 12,
                       soundOutputModeGet() == 1, mpegPlayInitColor);
            ret = movie_proc(movie_abort_check);
#ifdef ICO_HOST
            ico_gs_signal(ICO_GS_EV_FMV_END, ret == 1);
#endif
            sceGsSyncV(0);
            soundAllocIopHeap();
            AdpcmStreamHeap();
            ACTGame_SetActors_Debug(mpegPlayReturnStage, 1);
            gsb_UpdateGSSystem(1);
            gsb_UpdateGSSystem(1);
            gsb_Init(&db);
            mpegPlay = 0;
            stgmgrForceSwitchWithFade(mpegPlayReturnStage, 255.0f, mpegPlayFadeInSpeed);
            if (ret == 1) {
                stage_after_skipping_demo = 0xFFFFFFFE;
            }
            continue;
        }
        debug_ResetBar();
        MakeCollisionDependGObjList();
        MakeCharGObjList();
        ExecKeyInput();
#ifdef ICO_HOST
        /* PC port (R6a): developer mode (docs/port/DEVELOPER_MODE.md)
           restores the development build's debug menu call, here after the
           pad is read and before ExecIcoMisc's layout code can clear
           pad[0].flags.  SELECT opens the menu; until then debug_Menu only
           reads the pad.  Off: not called, as in retail. */
        if (ico_opt_developer_mode()) {
            debug_Menu();
        }
#endif
        ExecIcoMisc();
        if (graphics_ready == 0) {
            stage_ResetAnimation();
            stage_CalcAnimationNoParent();
        }
        iosOmMain();
        if (graphics_ready == 0) {
            stage_CalcAnimationParent();
        }
        iosOmCreateDL();
        ExecDelayFree();
        gsb_TakeSnap();
        frameReady = 1;
#ifdef ICO_HOST
        ico_host_main_tick();
#endif
        if (systemFault != 0) {
            break;
        }
    }
    while (1) {
        iosThreadSleep();
    }
}

/* .sbss: the idle thread's spin counters */
static int idleCount; /* derived name */

static int idleLoop; /* derived name */

static void idle(void)
{
    debug_StdPrintfDummy("idle() in\n");
    debug_StdPrintfDummy("--------------------------------------------------------------\n");
    iosThreadCreate(&cdvdThread, 6, iosCdvdManager, 0, cdvdThreadStack, sizeof(cdvdThreadStack),
                    0x1C);
    iosThreadStart(&cdvdThread);
    iosThreadCreate(&stageManagerThread, 7, StageManager, 0, stageManagerThreadStack,
                    sizeof(stageManagerThreadStack), 0x1B);
    iosThreadStart(&stageManagerThread);
    iosThreadCreate(&mcThread, 5, iosMcManager, 0, mcThreadStack, sizeof(mcThreadStack), 0x1B);
    iosThreadStart(&mcThread);
    iosThreadCreate(&jimakuThread, 9, jimakuManager, 0, jimakuThreadStack, 0x2000, 0x1B);
    iosThreadStart(&jimakuThread);
    iosThreadCreate(&soundThread, 8, sndManager, 0, soundThreadStack, sizeof(soundThreadStack),
                    0x10);
    iosThreadStart(&soundThread);
    iosThreadCreate(&mainThread, 3, Main, 0, mainThreadStack, sizeof(mainThreadStack), 0x1B);
    iosThreadStart(&mainThread);
    debug_StdPrintfDummy("--- loop continues infinitely ... ---\n");
    iosThreadSetPri(0, 0x20);
#ifdef ICO_HOST
    ico_host_milestone("idle: every thread started, idle loop");
#endif
    while (1) {
#ifdef ICO_HOST
        /* The busy loop holds the CPU until the next vsync: lower priorities
           (the finished processes parked at 0x21 and 0x22) never run, and
           the host gets control back once per vsync. */
        ico_sched_spin_vsync();
#endif
        idleCount++;
        if (idleCount < 10000000) {
            continue;
        }
        idleCount = 0;
        idleLoop++;
        debug_StdPrintfDummy("idle time:%d\n", idleLoop);
    }
}

/* vsyncs counted toward systemStatus[1], the frame step */
static int frameStepCount = 0; /* derived name */

static void scheduler(void)
{
    IosMsgWord msg[4];

    debug_StdPrintfDummy("scheduler() in\n");
    sceGsSyncV(0);
    iosMsgQueueCreate(&SchedulerMsgQ, schedulerMsgBuff, 8);
    iosMsgSetEvent(2, &SchedulerMsgQ, 2);
#ifdef ICO_HOST
    ico_host_milestone("scheduler: vsync event set");
#endif
    while (1) {
        iosMsgRecv(&SchedulerMsgQ, msg, 1);
        if (msg[0] == 2) {
            vsyncCount++;
            frameStepCount++;
            if (frameStepCount >= systemStatus[1] && stageManagerFreeResourceFlag == 0) {
                if (frameReady > 0) {
                    if (mpegPlay != 0) {
                        frameReady = 0;
                        goto wake;
                    }
                    if (gsb_SyncGSSystem() != 0) {
                        goto skip;
                    }
                    _PushVu0Registers();
                    gsb_UpdateGSSystem(0);
                    _PopVu0Registers();
                    if (systemStatus[5] != 0) {
                        if (iosCdvdDiskStatusGet() != 0) {
                            frameReady = 0;
                            goto wake;
                        }
                    }
                    lock_execIcoMisc++;
                }
                frameReady = 0;
            wake:
                iosThreadCancelWakeup(&mainThread);
                startStagePauseDisableTimer++;
                if (iosThreadWakeup(&mainThread) < 0) {
                    /* failed to start the main thread */
                    debug_StdPrintfDummy("メーンスレッドの起動失敗しました\n");
                }
                frameStepCount = 0;
            }
        skip:
            iosThreadWakeup(&soundThread);
            if (IosMcLock >= 0) {
                SignalSema(IosMcLock);
            }
            if (IosCdvdMgrSleep != 0 && (mpegPlay == 0 || iosCdvdBackGroundMgrRunning != 0)) {
                iosThreadWakeup(&cdvdThread);
            }
            if (stgMgrWakeupRequest != 0) {
                iosThreadWakeup(&stageManagerThread);
            }
            la_playtime_count();
        } else {
            /* an unknown message arrived */
            debug_StdPrintfDummy("不明なメッセージの着信を確認しました in Scheduler\n");
        }
    }
    /* unreachable: the loop above never exits */
    debug_StdPrintfDummy("scheduler() out\n");
}

#ifdef ICO_HOST

/* The host's vsync (port/platform/host_loop.c): the vblank-start interrupt.
   The GS's current field goes into GS_CSR.FIELD (bit 13), which the vblank
   handler (fumi/ios/message.c, signal_handler) reads into odd_even. The
   handler wakes the event thread iosMsgSetEvent made, whose message wakes
   scheduler() above, as on the PS2. */
void ico_vsync(int field_parity)
{
    if (field_parity) {
        *GS_CSR |= 1ull << 13;
    } else {
        *GS_CSR &= ~(1ull << 13);
    }
    ico_kernel_raise_intc(2);
}

#endif

static void boot(void)
{
    debug_StdPrintfDummy("boot()\n");
    debug_StdPrintfDummy("file init\n");
#ifdef ICO_HOST
    ico_host_name_func((void *)idle, "idle");
    ico_host_name_func((void *)scheduler, "scheduler");
    ico_host_milestone("boot: file_Init");
#endif
    file_Init();
    debug_StdPrintfDummy("iosInit\n");
#ifdef ICO_HOST
    ico_host_milestone("boot: iosInitialize");
#endif
    iosInitialize();
#ifdef ICO_HOST
    ico_host_milestone("boot: iosInitialize done");
#endif
    gflagInit();
    systemStatus[2] = 1;
    stage_no = 1;
    CheckPoint();
    iosThreadCreate(&idleThread, 1, idle, 0, idleThreadStack, sizeof(idleThreadStack), 0x1B);
    iosThreadStart(&idleThread);
    iosThreadCreate(&schedulerThread, 1, scheduler, 0, schedulerThreadStack,
                    sizeof(schedulerThreadStack), 0xF);
    iosThreadStart(&schedulerThread);
#ifdef ICO_HOST
    ico_host_milestone("boot: idle and scheduler started, boot thread sleeps");
#endif
    iosThreadSleep();
}

void Emergency_DestroyAllThread(void)
{
    int me = GetThreadId();
    ThreadTbl t = allThreads;
    unsigned int i;

    for (i = 0; i < 6; i++) {
        if (me != t.th[i]->id) {
            iosThreadDestroy(t.th[i]);
        }
    }
}

int movie_abort_check(void)
{
    int ret = 0;
    if (lastVsyncCount != vsyncCount) {
        lastVsyncCount = vsyncCount;
        ExecKeyInput();
        ret = 0;
        ret = (pad[0].flags & 0x800) != ret;
    }
    return ret;
}

void demoEnd(void) {}

/* the boot thread's kernel id */
static int bootThreadId; /* derived name */

int main(void)
{
    debug_StdPrintfDummy("main\n");
    debug_StdPrintfDummy("main\n");
    ChangeThreadPriority(GetThreadId(), 14);
    bootThreadId = GetThreadId();
    boot();
    return 0;
}

/* main.c's .sdata globals.  The PAL build adds six (marked derived): one
   quadword after game_pause and the girl-control block before boyGObj.  Each
   is a 4-byte scalar; the ones marked QWORD open a 16-byte quadword of their
   own, the rest of which stays empty. */
#define QWORD __attribute__((aligned(16)))

int buffer_ID QWORD = 0;

int odd_even QWORD = 0;

int frame_count QWORD = 0;

char *matrixptr QWORD = 0;

int debugMoveMode QWORD = 0;

int stage_no QWORD = 0;

int before_stage_no QWORD = 0;

int exit_no QWORD = 0;

int motionFrameUpdate QWORD = 0;

int interlace QWORD = 0;

int thisIsYourStartStage QWORD = 0;

int NonLinearCameraMove QWORD = 0;

int GlobalTimer QWORD = 0;

int lock_execIcoMisc QWORD = 0;

int graphics_ready QWORD = 0;

int game_pause QWORD = 0;

int data_loading QWORD = 0;

/* the PAL build's inserted, unreferenced quadword */
static int reservedWord QWORD = 0; /* derived name */

int screen_offset_x QWORD = 0;

int screen_offset_y QWORD = 0;

int fall_death_active QWORD = 0;

int title_fading_out QWORD = 0;

int collis_flg_stock QWORD = 0;

int IosPadLock QWORD = 0;

int IosCdLock QWORD = 0;

int IosSndLock QWORD = 0;

int IosStgMgrLock QWORD = 0;

int systemFault QWORD = 0;

/* the 0/1 game option that selects the pad word, held or pressed, the actions read */
int optionControlType QWORD = 0; /* derived name */

/* the five-way game option GsBase indexes its per-mode tint and blur rows with */
int optionScreenMode QWORD = 0; /* derived name */

/* ChangeGirlControlMode's flag, the game option that hands the girl to pad 2 */
int girlControlMode QWORD = 0; /* derived name */

GObj *boyGObj = 0;

GObj *girlGObj = 0;

/* the boy's and the girl's pad handles, the Act.pad each actor connects */
IosPadCtx *boyPad = 0;

IosPadCtx *girlPad = 0; /* derived name */

int gameover_flag = 0;

int gameover_layout_flag = 0;

/* ACTItemWatchMotion runs for the boy only while it is 0 */
int itemWatchOff = 0; /* derived name */

GObj *CurrentTargetGObj = 0;

GObj *CurrentTargetGObjSub QWORD = 0;

int current_stage_no = 0;

void (*system_stage_func)(void) = 0;

int InterStageSwitchLock = 0;
