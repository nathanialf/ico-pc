/*
 * port/platform/sched.c
 *
 * The EE thread scheduler over fibers. sched.h states the rules and their sources.
 *
 * Every switch goes through ico_sched_run (the dispatcher): a thread that
 * must give up the CPU yields to it, and it resumes the highest-priority
 * ready thread. The running thread stays linked in its ready queue (at the
 * head, unless it moved itself), so "preempted" needs no state of its own:
 * the dispatcher simply picks someone else first.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "diag_host.h"
#include "fiber.h"
#include "fpenv.h"
#include "sched.h"

typedef struct Thread {
    int used;
    int status; /* ICO_THS_*; a linked thread is READY (RUN when current) */
    int wait_type;
    int wait_id;
    int wait_result; /* what WaitSema returns once released */
    int init_priority;
    int priority;
    int wakeup_count;
    int spinning; /* busy-waits until vsync_count reaches spin_until */
    unsigned int spin_until;
    void (*entry)(void *);
    void *arg;
    void *stack;
    int stack_size;
    void *gp;
    unsigned int attr;
    unsigned int option;
    IcoFiber *fiber;
    int reap;           /* exited: the dispatcher destroys the fiber */
    int delete_on_reap; /* ExitDeleteThread: and frees the id */
    int linked;
    int rq_prev; /* ready queue links (thread ids, 0 = none) */
    int rq_next;
    int sq_next; /* semaphore wait queue link */
    int parent;  /* the thread that created it (diagnostics) */
    /* the last kernel call this thread made (ico_sched_note) */
    const char *note;
    int note_arg;
    void *note_caller;
    unsigned int note_vsync;
} Thread;

typedef struct Sema {
    int used;
    int count;
    int max_count;
    int init_count;
    unsigned int attr;
    unsigned int option;
    int num_wait;
    int wait_head; /* FIFO of waiting thread ids */
    int wait_tail;
} Sema;

static Thread threads[ICO_SCHED_MAX_THREADS];

static Sema semas[ICO_SCHED_MAX_SEMAS];

static int rq_head[ICO_SCHED_PRIORITIES];

static int rq_tail[ICO_SCHED_PRIORITIES];

static int current_id; /* the thread on the CPU, 0 on the host context */

static int last_id; /* the thread that ran last (iGetThreadId) */

static int dispatching;

static int interrupt_depth;

static unsigned int vsync_count;

static unsigned long switch_count;

static void (*fiber_start_hook)(void);
/* ico_sched_call_on_host's request from a fiber, made by ico_sched_run */
static void (*host_call_fn)(void *arg);

static void *host_call_arg;

static void fatal(const char *what)
{
    fprintf(stderr, "sched: %s\n", what);
    fflush(stderr);
    ico_diag_set_failure("sched: %s", what);
    abort();
}

/* Host work in the host FP mode; the simulation's mode afterwards, whatever
   the work (a driver, SDL) left in MXCSR or FPCR (fiber.h: the switch does
   not save it). */
static void run_host_call(void (*fn)(void *), void *arg)
{
    ico_fpenv_host_enter();
    fn(arg);
    ico_fpenv_sim_enter();
}

static Thread *thread_of(int id)
{
    if (id <= 0 || id >= ICO_SCHED_MAX_THREADS || !threads[id].used) {
        return NULL;
    }
    return &threads[id];
}

static Sema *sema_of(int id)
{
    if (id <= 0 || id >= ICO_SCHED_MAX_SEMAS || !semas[id].used) {
        return NULL;
    }
    return &semas[id];
}

static Thread *require_thread(const char *call)
{
    static char msg[96];
    if (current_id == 0) {
        snprintf(msg, sizeof msg, "%s called on the host context (it would block)", call);
        fatal(msg);
    }
    return &threads[current_id];
}

/* --- Ready queues ---------------------------------------------------------- */

static void link_tail(int id)
{
    Thread *t = &threads[id];
    int p = t->priority;
    t->rq_prev = rq_tail[p];
    t->rq_next = 0;
    if (rq_tail[p] != 0) {
        threads[rq_tail[p]].rq_next = id;
    } else {
        rq_head[p] = id;
    }
    rq_tail[p] = id;
    t->linked = 1;
}

static void unlink_ready(int id)
{
    Thread *t = &threads[id];
    int p = t->priority;
    if (!t->linked) {
        return;
    }
    if (t->rq_prev != 0) {
        threads[t->rq_prev].rq_next = t->rq_next;
    } else {
        rq_head[p] = t->rq_next;
    }
    if (t->rq_next != 0) {
        threads[t->rq_next].rq_prev = t->rq_prev;
    } else {
        rq_tail[p] = t->rq_prev;
    }
    t->rq_prev = t->rq_next = 0;
    t->linked = 0;
}

static int pick(void)
{
    int p;
    for (p = 0; p < ICO_SCHED_PRIORITIES; p++) {
        if (rq_head[p] != 0) {
            return rq_head[p];
        }
    }
    return 0;
}

/* After an operation that may change who should run: the calling thread
   gives the CPU to the dispatcher unless it is still the one to run. The
   interrupt variants and the host context never switch here. */
static void reschedule(int from_interrupt)
{
    Thread *t;
    if (from_interrupt || current_id == 0) {
        return;
    }
    t = &threads[current_id];
    if (t->linked && !t->spinning && pick() == current_id) {
        return;
    }
    ico_fiber_yield();
}

/* --- Semaphore wait queues ------------------------------------------------- */

static void sema_enqueue(Sema *s, int id)
{
    threads[id].sq_next = 0;
    if (s->wait_tail != 0) {
        threads[s->wait_tail].sq_next = id;
    } else {
        s->wait_head = id;
    }
    s->wait_tail = id;
    s->num_wait++;
}

static void sema_remove(Sema *s, int id)
{
    int prev = 0;
    int cur = s->wait_head;
    while (cur != 0 && cur != id) {
        prev = cur;
        cur = threads[cur].sq_next;
    }
    if (cur == 0) {
        return;
    }
    if (prev != 0) {
        threads[prev].sq_next = threads[cur].sq_next;
    } else {
        s->wait_head = threads[cur].sq_next;
    }
    if (s->wait_tail == id) {
        s->wait_tail = prev;
    }
    threads[cur].sq_next = 0;
    s->num_wait--;
}

/* Releases the first waiter with WaitSema's return value `result`. */
static void sema_release_first(Sema *s, int result)
{
    int id = s->wait_head;
    Thread *t = &threads[id];
    sema_remove(s, id);
    t->wait_type = ICO_TSW_NONE;
    t->wait_id = 0;
    t->wait_result = result;
    if (t->status == ICO_THS_WAIT) {
        t->status = ICO_THS_READY;
        link_tail(id);
    } else if (t->status == ICO_THS_WAITSUSPEND) {
        t->status = ICO_THS_SUSPEND;
    }
}

/* --- Threads --------------------------------------------------------------- */

static void thread_main(void *arg)
{
    Thread *t = (Thread *)arg;
    if (fiber_start_hook != NULL) {
        fiber_start_hook();
    }
    t->entry(t->arg);
    /* the EE starts a thread with its return address at ExitThread */
    ico_sched_exit_thread();
}

/* Back to the state CreateThread leaves (the EE resets a thread that exits
   or is terminated, so StartThread begins it afresh). */
static void reset_thread(Thread *t)
{
    t->status = ICO_THS_DORMANT;
    t->priority = t->init_priority;
    t->wakeup_count = 0;
    t->wait_type = ICO_TSW_NONE;
    t->wait_id = 0;
    t->spinning = 0;
}

int ico_sched_create_thread(void (*entry)(void *), void *stack, int stack_size, void *gp,
                            int init_priority, unsigned int attr, unsigned int option)
{
    int id;
    Thread *t;
    if (entry == NULL || init_priority < 0 || init_priority >= ICO_SCHED_PRIORITIES) {
        return -1;
    }
    for (id = 1; id < ICO_SCHED_MAX_THREADS; id++) {
        if (!threads[id].used) {
            break;
        }
    }
    if (id == ICO_SCHED_MAX_THREADS) {
        return -1;
    }
    t = &threads[id];
    memset(t, 0, sizeof *t);
    t->used = 1;
    t->entry = entry;
    t->stack = stack;
    t->stack_size = stack_size;
    t->gp = gp;
    t->init_priority = init_priority;
    t->attr = attr;
    t->option = option;
    t->parent = current_id;
    reset_thread(t);
    return id;
}

int ico_sched_delete_thread(int id)
{
    Thread *t = thread_of(id);
    if (t == NULL || id == current_id || t->status != ICO_THS_DORMANT) {
        return -1;
    }
    memset(t, 0, sizeof *t);
    return id;
}

int ico_sched_start_thread(int id, void *arg)
{
    Thread *t = thread_of(id);
    if (t == NULL || t->status != ICO_THS_DORMANT) {
        return -1;
    }
    t->arg = arg;
    t->fiber = ico_fiber_create(thread_main, t, ICO_FIBER_STACK_SIZE);
    if (t->fiber == NULL) {
        fatal("cannot create a fiber");
    }
    t->status = ICO_THS_READY;
    link_tail(id);
    reschedule(0);
    return id;
}

static void exit_current(int delete_after)
{
    Thread *t = require_thread("ExitThread");
    unlink_ready(current_id);
    reset_thread(t);
    t->reap = 1;
    t->delete_on_reap = delete_after;
    ico_fiber_yield();
    fatal("an exited thread was resumed");
}

void ico_sched_exit_thread(void)
{
    exit_current(0);
}

void ico_sched_exit_delete_thread(void)
{
    exit_current(1);
}

int ico_sched_terminate_thread(int id)
{
    Thread *t = thread_of(id);
    if (t == NULL || id == current_id || t->status == ICO_THS_DORMANT) {
        return -1;
    }
    unlink_ready(id);
    if (t->wait_type == ICO_TSW_SEMA) {
        Sema *s = sema_of(t->wait_id);
        if (s != NULL) {
            sema_remove(s, id);
        }
    }
    reset_thread(t);
    ico_fiber_destroy(t->fiber);
    t->fiber = NULL;
    return id;
}

int ico_sched_change_priority(int id, int priority, int from_interrupt)
{
    Thread *t = thread_of(id);
    int old;
    if (t == NULL || priority < 0 || priority >= ICO_SCHED_PRIORITIES) {
        return -1;
    }
    old = t->priority;
    if (t->linked) {
        /* to the tail of the new priority's queue, even when unchanged */
        unlink_ready(id);
        t->priority = priority;
        link_tail(id);
    } else {
        t->priority = priority;
    }
    reschedule(from_interrupt);
    return old;
}

int ico_sched_rotate_ready_queue(int priority, int from_interrupt)
{
    int id;
    if (priority < 0 || priority >= ICO_SCHED_PRIORITIES) {
        return -1;
    }
    id = rq_head[priority];
    if (id != 0 && threads[id].rq_next != 0) {
        unlink_ready(id);
        link_tail(id);
    }
    reschedule(from_interrupt);
    return priority;
}

int ico_sched_get_thread_id(void)
{
    return current_id != 0 ? current_id : last_id;
}

int ico_sched_refer_thread(int id, IcoThreadInfo *info)
{
    Thread *t;
    if (id < 0 || id >= ICO_SCHED_MAX_THREADS) {
        return -1;
    }
    if (id == 0) {
        id = current_id;
    }
    t = thread_of(id);
    if (t == NULL) {
        return 0;
    }
    info->status = id == current_id ? ICO_THS_RUN : t->status;
    info->entry = t->entry;
    info->stack = t->stack;
    info->stack_size = t->stack_size;
    info->gp = t->gp;
    info->init_priority = t->init_priority;
    info->current_priority = t->priority;
    info->attr = t->attr;
    info->option = t->option;
    info->wait_type = t->wait_type;
    info->wait_id = t->wait_id;
    info->wakeup_count = t->wakeup_count;
    return info->status;
}

int ico_sched_sleep(void)
{
    int id = current_id;
    Thread *t;
    if (id != 0 && threads[id].wakeup_count > 0) {
        threads[id].wakeup_count--;
        return id;
    }
    t = require_thread("SleepThread");
    unlink_ready(id);
    t->status = ICO_THS_WAIT;
    t->wait_type = ICO_TSW_SLEEP;
    t->wait_id = 0;
    reschedule(0);
    return id;
}

int ico_sched_wakeup(int id, int from_interrupt)
{
    Thread *t = thread_of(id);
    if (t == NULL || id == current_id || t->status == ICO_THS_DORMANT) {
        return -1;
    }
    if (t->wait_type == ICO_TSW_SLEEP) {
        t->wait_type = ICO_TSW_NONE;
        if (t->status == ICO_THS_WAIT) {
            t->status = ICO_THS_READY;
            link_tail(id);
            reschedule(from_interrupt);
        } else { /* WAITSUSPEND */
            t->status = ICO_THS_SUSPEND;
        }
    } else {
        t->wakeup_count++;
    }
    return id;
}

int ico_sched_cancel_wakeup(int id)
{
    Thread *t = thread_of(id);
    int n;
    if (t == NULL) {
        return -1;
    }
    n = t->wakeup_count;
    t->wakeup_count = 0;
    return n;
}

int ico_sched_suspend(int id, int from_interrupt)
{
    Thread *t = thread_of(id);
    if (t == NULL || (t->status != ICO_THS_READY && t->status != ICO_THS_WAIT)) {
        return -1;
    }
    if (t->status == ICO_THS_READY) {
        unlink_ready(id);
        t->status = ICO_THS_SUSPEND;
    } else {
        t->status = ICO_THS_WAITSUSPEND;
    }
    reschedule(from_interrupt);
    return id;
}

int ico_sched_resume(int id, int from_interrupt)
{
    Thread *t = thread_of(id);
    if (t == NULL || id == current_id) {
        return -1;
    }
    if (t->status == ICO_THS_SUSPEND) {
        t->status = ICO_THS_READY;
        link_tail(id);
    } else if (t->status == ICO_THS_WAITSUSPEND) {
        t->status = ICO_THS_WAIT;
    } else {
        return -1;
    }
    reschedule(from_interrupt);
    return id;
}

/* --- Semaphores ------------------------------------------------------------ */

int ico_sched_create_sema(int init_count, int max_count, unsigned int attr, unsigned int option)
{
    int id;
    Sema *s;
    if (init_count < 0) {
        return -1;
    }
    for (id = 1; id < ICO_SCHED_MAX_SEMAS; id++) {
        if (!semas[id].used) {
            break;
        }
    }
    if (id == ICO_SCHED_MAX_SEMAS) {
        return -1;
    }
    s = &semas[id];
    memset(s, 0, sizeof *s);
    s->used = 1;
    s->count = init_count;
    s->init_count = init_count;
    s->max_count = max_count;
    s->attr = attr;
    s->option = option;
    return id;
}

int ico_sched_delete_sema(int id)
{
    Sema *s = sema_of(id);
    if (s == NULL) {
        return -1;
    }
    /* every waiter is released and its WaitSema fails */
    while (s->num_wait > 0) {
        sema_release_first(s, -1);
    }
    memset(s, 0, sizeof *s);
    reschedule(0);
    return id;
}

int ico_sched_signal_sema(int id, int from_interrupt)
{
    Sema *s = sema_of(id);
    if (s == NULL) {
        return -1;
    }
    if (s->num_wait > 0) {
        sema_release_first(s, id);
        reschedule(from_interrupt);
    } else {
        /* no maximum check (open case) */
        s->count++;
    }
    return id;
}

int ico_sched_wait_sema(int id)
{
    Sema *s = sema_of(id);
    Thread *t;
    if (s == NULL) {
        return -1;
    }
    if (s->count > 0) {
        s->count--;
        return id;
    }
    t = require_thread("WaitSema");
    unlink_ready(current_id);
    t->status = ICO_THS_WAIT;
    t->wait_type = ICO_TSW_SEMA;
    t->wait_id = id;
    t->wait_result = id;
    sema_enqueue(s, current_id);
    reschedule(0);
    return t->wait_result;
}

int ico_sched_poll_sema(int id)
{
    Sema *s = sema_of(id);
    if (s == NULL || s->count == 0) {
        return -1;
    }
    s->count--;
    return id;
}

int ico_sched_refer_sema(int id, IcoSemaInfo *info)
{
    Sema *s = sema_of(id);
    if (s == NULL) {
        return -1;
    }
    info->count = s->count;
    info->max_count = s->max_count;
    info->init_count = s->init_count;
    info->num_wait = s->num_wait;
    info->attr = s->attr;
    info->option = s->option;
    return id;
}

/* --- The dispatcher and the host side -------------------------------------- */

void ico_sched_reset(void)
{
    int id;
    if (current_id != 0 || dispatching) {
        fatal("ico_sched_reset called from a thread");
    }
    for (id = 1; id < ICO_SCHED_MAX_THREADS; id++) {
        if (threads[id].fiber != NULL) {
            ico_fiber_destroy(threads[id].fiber);
        }
    }
    memset(threads, 0, sizeof threads);
    memset(semas, 0, sizeof semas);
    memset(rq_head, 0, sizeof rq_head);
    memset(rq_tail, 0, sizeof rq_tail);
    last_id = 0;
    interrupt_depth = 0;
    vsync_count = 0;
    switch_count = 0;
    host_call_fn = NULL;
}

void ico_sched_set_fiber_start_hook(void (*hook)(void))
{
    fiber_start_hook = hook;
}

void ico_sched_call_on_host(void (*fn)(void *arg), void *arg)
{
    if (current_id == 0) {
        run_host_call(fn, arg);
        return;
    }
    host_call_fn = fn;
    host_call_arg = arg;
    ico_fiber_yield();
}

int ico_sched_boot(void (*entry)(void *), void *arg, int priority)
{
    int id = ico_sched_create_thread(entry, NULL, 0, NULL, priority, 0, 0);
    if (id < 0 || ico_sched_start_thread(id, arg) < 0) {
        fatal("cannot create the boot thread");
    }
    return id;
}

int ico_sched_run(void)
{
    if (current_id != 0 || dispatching) {
        fatal("ico_sched_run called from a thread");
    }
    if (interrupt_depth != 0) {
        fatal("ico_sched_run called inside an interrupt");
    }
    dispatching = 1;
    for (;;) {
        int id = pick();
        Thread *t;
        if (id == 0) {
            dispatching = 0;
            return ICO_SCHED_EMPTY;
        }
        t = &threads[id];
        if (t->spinning) {
            if ((int)(vsync_count - t->spin_until) < 0) {
                dispatching = 0;
                return ICO_SCHED_SPINNING;
            }
            t->spinning = 0;
        }
        current_id = last_id = id;
        switch_count++;
        if (ico_fiber_resume(t->fiber) != 0) {
            fatal("cannot resume a thread's fiber");
        }
        /* ico_sched_call_on_host: the call on this stack, then the same
           thread again */
        while (host_call_fn != NULL) {
            void (*fn)(void *) = host_call_fn;
            host_call_fn = NULL;
            current_id = 0;
            run_host_call(fn, host_call_arg);
            current_id = id;
            if (ico_fiber_resume(t->fiber) != 0) {
                fatal("cannot resume a thread's fiber");
            }
        }
        current_id = 0;
        if (t->reap) {
            ico_fiber_destroy(t->fiber);
            t->fiber = NULL;
            t->reap = 0;
            if (t->delete_on_reap) {
                memset(t, 0, sizeof *t);
            }
        }
    }
}

void ico_sched_vsync_advance(void)
{
    vsync_count++;
}

unsigned int ico_sched_vsync_count(void)
{
    return vsync_count;
}

void ico_sched_interrupt_begin(void)
{
    if (current_id != 0) {
        fatal("an interrupt raised from a thread");
    }
    interrupt_depth++;
}

void ico_sched_interrupt_end(void)
{
    interrupt_depth--;
}

int ico_sched_in_interrupt(void)
{
    return interrupt_depth != 0;
}

unsigned long ico_sched_switch_count(void)
{
    return switch_count;
}

void ico_sched_spin_vsync(void)
{
    Thread *t = require_thread("a vsync busy-wait");
    t->spinning = 1;
    t->spin_until = vsync_count + 1;
    ico_fiber_yield();
}

int ico_sched_in_thread(void)
{
    return current_id != 0;
}

/* --- Diagnostics ------------------------------------------------------------ */

void ico_sched_note(const char *what, int arg, void *caller)
{
    Thread *t;
    if (current_id == 0) {
        return;
    }
    t = &threads[current_id];
    t->note = what;
    t->note_arg = arg;
    t->note_caller = caller;
    t->note_vsync = vsync_count;
}

int ico_sched_current(void)
{
    return current_id;
}

int ico_sched_view(int id, IcoSchedView *v)
{
    Thread *t;
    memset(v, 0, sizeof *v);
    if (id <= 0 || id >= ICO_SCHED_MAX_THREADS || !threads[id].used) {
        return 0;
    }
    t = &threads[id];
    v->status = id == current_id ? ICO_THS_RUN : t->status;
    v->priority = t->priority;
    v->init_priority = t->init_priority;
    v->wait_type = t->wait_type;
    v->wait_id = t->wait_id;
    v->wakeup_count = t->wakeup_count;
    v->spinning = t->spinning;
    v->spin_until = t->spin_until;
    v->parent = t->parent;
    v->entry = (void *)t->entry;
    v->note = t->note;
    v->note_arg = t->note_arg;
    v->note_caller = t->note_caller;
    v->note_vsync = t->note_vsync;
    return 1;
}

int ico_sched_sema_view(int id, int *count, int *num_wait)
{
    Sema *s = sema_of(id);
    if (s == NULL) {
        return 0;
    }
    *count = s->count;
    *num_wait = s->num_wait;
    return 1;
}
