/*
 * port/platform/test/sched_test.c
 *
 * The fiber scheduler against the EE kernel rules sched.h states, through
 * the SDK calls the game makes (kernel_host.c). Each case logs the order
 * threads run in and compares it with the order the EE gives. Exit status
 * 0 on success.
 */
#include <stdio.h>
#include <string.h>

#include <eekernel.h>

#include "fiber.h"
#include "fpenv.h"
#include "kernel_host.h"
#include "sched.h"

static int fails;
static char log_buf[256];

static void logs(const char *s)
{
    if (log_buf[0] != 0) {
        strncat(log_buf, " ", sizeof log_buf - strlen(log_buf) - 1);
    }
    strncat(log_buf, s, sizeof log_buf - strlen(log_buf) - 1);
}

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

#define CHECK_LOG(want)                                                                            \
    do {                                                                                           \
        if (strcmp(log_buf, want) != 0) {                                                          \
            printf("FAIL %s:%d: order \"%s\", want \"%s\"\n", __FILE__, __LINE__, log_buf, want);  \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static void begin(const char *name)
{
    printf("-- %s\n", name);
    ico_sched_reset();
    ico_kernel_reset();
    log_buf[0] = 0;
}

static int make(void (*fn)(void *), int priority)
{
    struct ThreadParam p;
    memset(&p, 0, sizeof p);
    p.entry = fn;
    p.initPriority = priority;
    return CreateThread(&p);
}

static int start(void (*fn)(void *), int priority, void *arg)
{
    int id = make(fn, priority);
    CHECK(id > 0);
    CHECK(StartThread(id, arg) == id);
    return id;
}

static int status_of(int id)
{
    struct ThreadParam p;
    return ReferThreadStatus(id, &p);
}

static int priority_of(int id)
{
    struct ThreadParam p;
    ReferThreadStatus(id, &p);
    return p.currentPriority;
}

/* thread ids the bodies refer to */
static int tid_h;
static int tid_t;
static int sema_id;

/* a thread that logs its argument each time it runs and sleeps */
static void sleeper(void *arg)
{
    for (;;) {
        logs((const char *)arg);
        SleepThread();
    }
}

/* --- 1. a wakeup of a higher priority switches at once --------------------- */

static void preempt_low(void *arg)
{
    (void)arg;
    logs("L1");
    CHECK(WakeupThread(tid_h) == tid_h);
    logs("L2");
    SleepThread();
}

static void test_preempt_on_wakeup(void)
{
    begin("wakeup of a higher priority preempts");
    tid_h = start(sleeper, 10, "H");
    start(preempt_low, 20, NULL);
    CHECK(ico_sched_run() == ICO_SCHED_EMPTY);
    CHECK_LOG("H L1 H L2");
}

/* --- 2. FIFO within a priority; no switch to an equal or lower one ---------- */

static void starter(void *arg)
{
    (void)arg;
    start(sleeper, 20, "A");
    logs("s");
    start(sleeper, 20, "B");
    logs("s");
    start(sleeper, 30, "D");
    logs("s");
    start(sleeper, 20, "C");
    logs("s");
    SleepThread();
}

static void test_fifo(void)
{
    begin("FIFO within a priority");
    start(starter, 5, NULL);
    ico_sched_run();
    CHECK_LOG("s s s s A B C D");
}

/* --- 3. a preempted thread resumes before its peers ------------------------ */

static void peer1(void *arg)
{
    (void)arg;
    logs("1a");
    WakeupThread(tid_h);
    logs("1b");
    SleepThread();
}

static void test_preempted_resumes_first(void)
{
    begin("a preempted thread keeps its place");
    tid_h = start(sleeper, 10, "h");
    start(peer1, 20, NULL);
    start(sleeper, 20, "2");
    ico_sched_run();
    CHECK_LOG("h 1a h 1b 2");
}

/* --- 4. wakeup counts -------------------------------------------------------- */

static void counted(void *arg)
{
    (void)arg;
    SleepThread();
    logs("a");
    SleepThread();
    logs("b");
    SleepThread();
    logs("c");
}

static void test_wakeup_counts(void)
{
    struct ThreadParam p;
    int t;
    begin("a wakeup before the sleep is remembered");
    t = start(counted, 20, NULL);
    CHECK(WakeupThread(t) == t);
    CHECK(WakeupThread(t) == t);
    CHECK(ReferThreadStatus(t, &p) == ICO_THS_READY && p.wakeupCount == 2);
    ico_sched_run();
    CHECK_LOG("a b");
    CHECK(ReferThreadStatus(t, &p) == ICO_THS_WAIT && p.waitType == ICO_TSW_SLEEP);
    CHECK(p.wakeupCount == 0);
    WakeupThread(t);
    ico_sched_run();
    CHECK_LOG("a b c");
    CHECK(status_of(t) == ICO_THS_DORMANT);
    CHECK(WakeupThread(t) == -1); /* dormant */
    CHECK(WakeupThread(0) == -1);

    begin("CancelWakeupThread");
    t = start(counted, 20, NULL);
    WakeupThread(t);
    WakeupThread(t);
    WakeupThread(t);
    CHECK(CancelWakeupThread(t) == 3);
    CHECK(CancelWakeupThread(t) == 0);
    ico_sched_run();
    CHECK_LOG("");
    CHECK(status_of(t) == ICO_THS_WAIT);
}

/* --- 5. semaphores: FIFO waiters, counts, deletion ---------------------------- */

static void waiter(void *arg)
{
    char w[8] = "w";
    strcat(w, (const char *)arg);
    logs(w);
    CHECK(WaitSema(sema_id) == sema_id);
    logs((const char *)arg);
    SleepThread();
}

static void signaller(void *arg)
{
    (void)arg;
    logs("s");
    SignalSema(sema_id);
    logs("s");
    SignalSema(sema_id);
    logs("s");
    SleepThread();
}

static void test_sema_order(void)
{
    struct SemaParam sp;
    begin("semaphore waiters are released in wait order");
    memset(&sp, 0, sizeof sp);
    sp.initCount = 0;
    sp.maxCount = 4;
    sema_id = CreateSema(&sp);
    CHECK(sema_id > 0);
    /* the low-priority thread waits first */
    start(waiter, 30, "A");
    ico_sched_run();
    start(waiter, 10, "B");
    ico_sched_run();
    CHECK(ReferSemaStatus(sema_id, &sp) == sema_id && sp.numWaitThreads == 2 &&
          sp.currentCount == 0 && sp.maxCount == 4);
    start(signaller, 40, NULL);
    ico_sched_run();
    /* each release preempts the lower-priority signaller */
    CHECK_LOG("wA wB s A s B s");
    CHECK(ReferSemaStatus(sema_id, &sp) == sema_id && sp.numWaitThreads == 0);
}

static int wait_result;

static void wait_once(void *arg)
{
    (void)arg;
    wait_result = WaitSema(sema_id);
}

static void test_sema_counts(void)
{
    struct SemaParam sp;
    begin("semaphore counts, PollSema, DeleteSema");
    memset(&sp, 0, sizeof sp);
    sp.initCount = 1;
    sp.maxCount = 1;
    sema_id = CreateSema(&sp);
    CHECK(PollSema(sema_id) == sema_id);
    CHECK(PollSema(sema_id) == -1);
    /* no waiter: the count goes up, with no check against maxCount */
    SignalSema(sema_id);
    iSignalSema(sema_id);
    ReferSemaStatus(sema_id, &sp);
    CHECK(sp.currentCount == 2);
    CHECK(WaitSema(sema_id) == sema_id); /* a count: no wait, fine on the host */
    CHECK(PollSema(sema_id) == sema_id);
    /* a waiter released by DeleteSema gets -1 */
    wait_result = 0;
    start(wait_once, 20, NULL);
    ico_sched_run();
    CHECK(DeleteSema(sema_id) == sema_id);
    ico_sched_run();
    CHECK(wait_result == -1);
    CHECK(SignalSema(sema_id) == -1);
    CHECK(ReferSemaStatus(sema_id, &sp) == -1);
}

/* --- 6. exit, restart, terminate, delete ------------------------------------ */

static void returns(void *arg)
{
    logs((const char *)arg);
}

static void exits_midway(void *arg)
{
    (void)arg;
    logs("a");
    ExitThread();
    logs("never");
}

static int self_terminate;

static void terminates_self(void *arg)
{
    (void)arg;
    self_terminate = TerminateThread(GetThreadId());
    CHECK(DeleteThread(GetThreadId()) == -1);
}

static void test_exit_delete(void)
{
    struct SemaParam sp;
    int t;
    begin("thread exit, restart and deletion");
    t = start(returns, 20, "x");
    ico_sched_run();
    CHECK(status_of(t) == ICO_THS_DORMANT);
    CHECK(StartThread(t, "y") == t); /* begins again at its entry */
    ico_sched_run();
    CHECK_LOG("x y");
    CHECK(DeleteThread(t) == t);
    CHECK(status_of(t) == 0);
    CHECK(StartThread(t, "z") == -1);

    t = start(exits_midway, 20, NULL);
    ico_sched_run();
    CHECK_LOG("x y a");
    CHECK(status_of(t) == ICO_THS_DORMANT);

    t = start(terminates_self, 20, NULL);
    ico_sched_run();
    CHECK(self_terminate == -1);

    /* terminating a sleeping thread, then one waiting on a semaphore */
    t = start(sleeper, 20, "s");
    ico_sched_run();
    CHECK(DeleteThread(t) == -1); /* not dormant */
    CHECK(TerminateThread(t) == t);
    CHECK(status_of(t) == ICO_THS_DORMANT);
    CHECK(TerminateThread(t) == -1);
    CHECK(DeleteThread(t) == t);

    memset(&sp, 0, sizeof sp);
    sp.maxCount = 1;
    sema_id = CreateSema(&sp);
    t = start(wait_once, 20, NULL);
    ico_sched_run();
    ReferSemaStatus(sema_id, &sp);
    CHECK(sp.numWaitThreads == 1);
    CHECK(TerminateThread(t) == t);
    ReferSemaStatus(sema_id, &sp);
    CHECK(sp.numWaitThreads == 0);
    SignalSema(sema_id);
    ReferSemaStatus(sema_id, &sp);
    CHECK(sp.currentCount == 1);
}

/* --- 7. the finished-process convention (priority 0x22) ---------------------- */

static void idle_body(void *arg)
{
    (void)arg;
    ChangeThreadPriority(GetThreadId(), 0x20);
    for (;;) {
        ico_sched_spin_vsync();
    }
}

/* fumi/ios/thread.c's iosThreadMain: the body returns, then the thread
   drops below the idle thread instead of exiting */
static void process(void *arg)
{
    (void)arg;
    logs("p");
    ChangeThreadPriority(GetThreadId(), 0x22);
    logs("never");
}

static void test_finished_process(void)
{
    struct ThreadParam p;
    int i;
    int t;
    begin("a finished process parks below the idle thread");
    start(idle_body, 0x1B, NULL);
    t = start(process, 0x13, NULL);
    CHECK(ico_sched_run() == ICO_SCHED_SPINNING);
    for (i = 0; i < 4; i++) {
        ico_sched_vsync_advance();
        CHECK(ico_sched_run() == ICO_SCHED_SPINNING);
    }
    CHECK_LOG("p");
    CHECK(ReferThreadStatus(t, &p) == ICO_THS_READY && p.currentPriority == 0x22);
    /* obj_manager.c sees 0x22 and has it destroyed */
    CHECK(TerminateThread(t) == t && DeleteThread(t) == t);
}

/* --- 8. priority changes and the ready-queue rotation ------------------------- */

static void requeue(void *arg)
{
    logs("a");
    if (arg != NULL) {
        RotateThreadReadyQueue(20);
    } else {
        ChangeThreadPriority(GetThreadId(), 20);
    }
    logs("a2");
    SleepThread();
}

static void raiser(void *arg)
{
    (void)arg;
    logs("r");
    CHECK(ChangeThreadPriority(tid_t, 10) == 30);
    logs("r2");
    SleepThread();
}

static void test_priority_changes(void)
{
    begin("ChangeThreadPriority to its own priority goes to the tail");
    start(requeue, 20, NULL);
    start(sleeper, 20, "b");
    ico_sched_run();
    CHECK_LOG("a b a2");

    begin("RotateThreadReadyQueue");
    start(requeue, 20, "rotate");
    start(sleeper, 20, "b");
    ico_sched_run();
    CHECK_LOG("a b a2");

    begin("raising a ready thread above the caller preempts");
    start(raiser, 20, NULL);
    tid_t = start(sleeper, 30, "t");
    ico_sched_run();
    CHECK_LOG("r t r2");
    CHECK(priority_of(tid_t) == 10);
    CHECK(ChangeThreadPriority(tid_t, 128) == -1);
}

/* --- 9. interrupts and busy waits ---------------------------------------------- */

static int vblank_handler(int cause)
{
    CHECK(cause == ICO_INTC_VBLANK_S);
    CHECK(ico_sched_in_interrupt());
    CHECK(iWakeupThread(tid_h) == tid_h);
    return 0;
}

static void spinner(void *arg)
{
    (void)arg;
    logs("s1");
    ico_sched_spin_vsync();
    logs("s2");
    SleepThread();
}

static void test_interrupts(void)
{
    begin("iWakeupThread from the vblank handler");
    tid_h = start(sleeper, 10, "h");
    ico_sched_run();
    CHECK(AddIntcHandler(ICO_INTC_VBLANK_S, vblank_handler, -1) > 0);
    CHECK(ico_kernel_raise_intc(ICO_INTC_VBLANK_S) == 0); /* not enabled yet */
    CHECK(status_of(tid_h) == ICO_THS_WAIT);
    CHECK(EnableIntc(ICO_INTC_VBLANK_S) == 1);
    CHECK(ico_kernel_raise_intc(ICO_INTC_VBLANK_S) == 1);
    /* no switch inside the interrupt: the thread is ready, not run */
    CHECK(status_of(tid_h) == ICO_THS_READY);
    CHECK_LOG("h");
    ico_sched_run();
    CHECK_LOG("h h");

    begin("a busy wait holds lower priorities until the vsync");
    tid_h = start(sleeper, 5, "h");
    start(spinner, 10, NULL);
    start(sleeper, 20, "low");
    AddIntcHandler(ICO_INTC_VBLANK_S, vblank_handler, -1);
    EnableIntc(ICO_INTC_VBLANK_S);
    CHECK(ico_sched_run() == ICO_SCHED_SPINNING);
    CHECK_LOG("h s1");
    /* the vsync: its interrupt wakes the higher thread, which runs first */
    ico_sched_vsync_advance();
    ico_kernel_raise_intc(ICO_INTC_VBLANK_S);
    CHECK(ico_sched_run() == ICO_SCHED_EMPTY);
    CHECK_LOG("h s1 h s2 low");
}

/* --- 10. suspend and resume ------------------------------------------------------ */

static void test_suspend(void)
{
    int t;
    begin("SuspendThread and ResumeThread");
    t = start(sleeper, 20, "t");
    CHECK(SuspendThread(t) == t);
    CHECK(status_of(t) == ICO_THS_SUSPEND);
    ico_sched_run();
    CHECK_LOG("");
    CHECK(ResumeThread(t) == t);
    ico_sched_run();
    CHECK_LOG("t");
    /* a sleeping thread suspended and woken stays suspended */
    CHECK(SuspendThread(t) == t && status_of(t) == ICO_THS_WAITSUSPEND);
    CHECK(WakeupThread(t) == t && status_of(t) == ICO_THS_SUSPEND);
    CHECK(ResumeThread(t) == t && status_of(t) == ICO_THS_READY);
    ico_sched_run();
    CHECK_LOG("t t");
    CHECK(ResumeThread(t) == -1);
}

/* --- 11. the fiber start hook and the boot thread -------------------------------- */

static int hook_calls;

static void count_hook(void)
{
    hook_calls++;
}

static int boot_saw_id;

static void boot_body(void *arg)
{
    (void)arg;
    boot_saw_id = GetThreadId();
    /* the game's main: ChangeThreadPriority(GetThreadId(), 14) */
    CHECK(ChangeThreadPriority(GetThreadId(), 14) == 1);
    start(sleeper, 15, "x");
    SleepThread();
}

static void test_boot(void)
{
    begin("the boot thread and the fiber start hook");
    hook_calls = 0;
    ico_sched_set_fiber_start_hook(count_hook);
    CHECK(ico_sched_boot(boot_body, NULL, 1) == 1);
    ico_sched_run();
    CHECK(boot_saw_id == 1);
    CHECK(hook_calls == 2);
    CHECK_LOG("x");
    ico_sched_set_fiber_start_hook(NULL);
}

/* --- 12. host calls: the host's stack and FP mode, the same thread after -------- */

static int host_on_fiber = -1;
static unsigned long long host_fp, host_fp_sim, after_fp;

static void host_work(void *arg)
{
    host_on_fiber = ico_fiber_current() != NULL;
    host_fp = ico_fpenv_raw();
    logs((const char *)arg);
}

static void caller(void *arg)
{
    (void)arg;
    logs("a");
    ico_sched_call_on_host(host_work, "host");
    after_fp = ico_fpenv_raw();
    logs("b");
    SleepThread();
}

static void test_call_on_host(void)
{
    begin("ico_sched_call_on_host");
    ico_fpenv_sim_enter();
    host_fp_sim = ico_fpenv_raw();
    ico_sched_set_fiber_start_hook(ico_fpenv_sim_enter);
    start(caller, 10, NULL);
    start(sleeper, 10, "x"); /* same priority: must not run between a and b */
    ico_sched_run();
    CHECK_LOG("a host b x");
    CHECK(host_on_fiber == 0);
    CHECK(host_fp != host_fp_sim);
    CHECK(after_fp == host_fp_sim);
    /* from the host context: a direct call, the simulation's mode after */
    log_buf[0] = 0;
    ico_sched_call_on_host(host_work, "direct");
    CHECK_LOG("direct");
    CHECK(ico_fpenv_raw() == host_fp_sim);
    ico_sched_set_fiber_start_hook(NULL);
    ico_fpenv_host_enter();
}

int main(void)
{
    printf("fiber backend: %s\n", ico_fiber_backend());
    test_preempt_on_wakeup();
    test_fifo();
    test_preempted_resumes_first();
    test_wakeup_counts();
    test_sema_order();
    test_sema_counts();
    test_exit_delete();
    test_finished_process();
    test_priority_changes();
    test_interrupts();
    test_suspend();
    test_boot();
    test_call_on_host();
    ico_sched_reset();
    if (fails != 0) {
        printf("%d failure(s)\n", fails);
        return 1;
    }
    printf("all passed\n");
    return 0;
}
