/*
 * port/platform/test/ios_chain_test.c
 *
 * The game's own thread and message layer (fumi/ios/thread.c, message.c,
 * memory.c, compiled unchanged with the game's options) on the fiber
 * scheduler, driven by simulated vsyncs the way host_loop.c drives them:
 *
 *   vsync -> INTC 2 -> message.c's signal_handler (reads GS_CSR.FIELD into
 *   odd_even, iWakeupThread of the event thread) -> the event thread sends
 *   to the scheduler's queue -> a scheduler loop shaped like main.c's wakes
 *   "Main" every second vsync -> Main wakes an actor process (priority
 *   0x13) that ends with the 0x22 convention and is torn down by thread.c's
 *   destroy manager.
 *
 * It asserts event order and thread state, not EE addresses, so it runs on
 * the host's own record layout.
 */
#include <stdio.h>
#include <string.h>

#include <eekernel.h>
#include <eeregs.h>

#include "ios.h"
#include "memory.h"
#include "message.h"
#include "thread.h"

#include "../arena.h"
#include "../kernel_host.h"
#include "../sched.h"

static int fails;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

/* What the rest of the game would define. */
void debug_StdPrintfDummy(const char *fmt, ...);
void debug_assertMessage(const char *file, int line, const char *mes);
void debug_assert(const char *file, int line);

void debug_StdPrintfDummy(const char *fmt, ...)
{
    (void)fmt;
}

void debug_assertMessage(const char *file, int line, const char *mes)
{
    printf("FAIL debug_assertMessage %s:%d: %s\n", file, line, mes);
    fails++;
}

void debug_assert(const char *file, int line)
{
    printf("FAIL debug_assert %s:%d\n", file, line);
    fails++;
}

int odd_even;
IosMemPart *ios_partition_root;
IosMemPart *ios_partition_event;

/* --- the miniature game ------------------------------------------------------ */

#define FRAME_STEP 2

static IOSThread idleTh;
static IOSThread schedTh;
static IOSThread mainTh;
static IOSThread procTh;
static char idleStack[0x1800] __attribute__((aligned(16)));
static char schedStack[0x1800] __attribute__((aligned(16)));
static char mainStack[0x1800] __attribute__((aligned(16)));
static IosMsgQueue schedQ;
static IosMsgWord schedBuf[8]; /* a message is pointer-wide on the host */

static int vsync_msgs;
static int parity_log[16];
static int main_ticks;
static int proc_runs;
static int proc_destroyed;
static char order[256];

static void note(const char *s)
{
    strncat(order, s, sizeof order - strlen(order) - 1);
}

static void idle(void)
{
    iosThreadSetPri(0, 0x20);
    for (;;) {
        ico_sched_spin_vsync();
    }
}

/* main.c's scheduler(), reduced to the frame step and the Main wakeup */
static void scheduler(void)
{
    IosMsgWord msg[4];
    int step = 0;
    iosMsgQueueCreate(&schedQ, schedBuf, 8);
    iosMsgSetEvent(2, &schedQ, 2);
    for (;;) {
        iosMsgRecv(&schedQ, msg, 1);
        CHECK(msg[0] == 2);
        if (vsync_msgs < 16) {
            parity_log[vsync_msgs] = odd_even;
        }
        vsync_msgs++;
        note("v");
        if (++step >= FRAME_STEP) {
            iosThreadCancelWakeup(&mainTh);
            iosThreadWakeup(&mainTh);
            step = 0;
        }
    }
}

/* an actor process: runs once per Main tick, three times, then ends */
static void process(void *arg)
{
    int i;
    (void)arg;
    for (i = 0; i < 3; i++) {
        proc_runs++;
        note("p");
        iosThreadSleep();
    }
}

static void Main(void)
{
    for (;;) {
        iosThreadCancelWakeup(0);
        iosThreadSleep();
        main_ticks++;
        note("M");
        if (main_ticks == 1) {
            iosThreadCreateS(&procTh, 1, process, 0, ios_partition_event, 0x1800, 0x13);
            procTh.sleeping = 1; /* act.c's actCreateSubThread */
            iosThreadStart(&procTh);
        } else if (!proc_destroyed) {
            if (iosThreadGetPri(&procTh) != 0x22) {
                iosThreadWakeup(&procTh);
            } else {
                iosThreadDestroy(&procTh); /* the destroy manager preempts */
                proc_destroyed = 1;
                note("d");
            }
        }
        note("m");
    }
}

static void boot(void *arg)
{
    (void)arg;
    ChangeThreadPriority(GetThreadId(), 14);
    iosThreadInit();
    ios_partition_root =
        iosMallocInitPartition(ico_arena_ee_addr(0x760000), ico_arena_ee_addr(0x1FEFFF0));
    ios_partition_event = iosMallocSetPartition(ios_partition_root, 262144, 16);
    iosMsgInit();
    iosThreadCreate(&idleTh, 1, idle, 0, idleStack, sizeof idleStack, 0x1B);
    iosThreadStart(&idleTh);
    iosThreadCreate(&schedTh, 1, scheduler, 0, schedStack, sizeof schedStack, 0xF);
    iosThreadStart(&schedTh);
    iosThreadCreate(&mainTh, 3, Main, 0, mainStack, sizeof mainStack, 0x1B);
    iosThreadStart(&mainTh);
    iosThreadSleep();
}

static void vsync(int field)
{
    ico_sched_vsync_advance();
    if (field) {
        *GS_CSR |= 1ull << 13;
    } else {
        *GS_CSR &= ~(1ull << 13);
    }
    ico_kernel_raise_intc(2);
    CHECK(ico_sched_run() == ICO_SCHED_SPINNING);
}

int main(void)
{
    struct ThreadParam tp;
    int i;
    int proc_id = 0;

    CHECK(ico_arena_init() == 0);
    ico_sched_reset();
    ico_kernel_reset();
    ico_sched_boot(boot, NULL, 1);
    CHECK(ico_sched_run() == ICO_SCHED_SPINNING);
    CHECK(ico_kernel_intc_enabled(2));
    CHECK(th_sig != 0);
    for (i = 0; i < 10; i++) {
        vsync(i & 1);
        if (i == 1) {
            proc_id = procTh.id;
        }
    }
    printf("order: %s\n", order);
    CHECK(vsync_msgs == 10);
    CHECK(main_ticks == 5);
    /* the field alternates; odd_even is its inverse */
    for (i = 0; i < 10; i++) {
        CHECK(parity_log[i] == ((i & 1) ^ 1));
    }
    /* tick 1 creates the process (0x13 preempts Main at once), ticks 2 and 3
       wake it, tick 4 wakes it to finish (its body returns and it parks at
       0x22, below the idle thread), tick 5 finds 0x22 and destroys it */
    CHECK(strcmp(order, "vvMpmvvMpmvvMpmvvMmvvMdm") == 0);
    CHECK(proc_runs == 3 && proc_destroyed);
    CHECK(ReferThreadStatus(proc_id, &tp) == 0); /* deleted by the manager */
    ico_sched_reset();

    if (fails != 0) {
        printf("%d failure(s)\n", fails);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
